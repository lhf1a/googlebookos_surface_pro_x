// SPDX-License-Identifier: GPL-2.0-only
/* Partial Iris1 backend. No call site is enabled until platform and HFI resource
 * lifetime are adapted. Never use devm for memory potentially owned by PAS9.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <asm/virt.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include "sq2_video_fw_layout.h"
#include "sq2-fw-transaction.h"
#include "sq2-venus-fw.h"
#include "sq2-pil-region.h"
#include "sq2-venus-no-tz.h"

struct sq2_venus_fw {
 struct sq2_fw_transaction tx;
 struct platform_device *pdev;
 const struct firmware *image; /* only retained during synchronous boot */
 void *cpu;
 dma_addr_t dma;
 phys_addr_t phys;
 bool pil_gap;
 void __iomem *no_tz_base;
 struct device *parent;
 struct platform_device *stream;
 struct iommu_domain *domain;
 bool attached, mapped, cpu_touched;
 struct resource region;
};
/* This owner and module pin outlive a failed core probe or driver teardown.
 * One PAS9 owner per board; failed shutdown blocks all further boot attempts.
 */
static DEFINE_MUTEX(sq2_fw_lock);
static struct sq2_venus_fw *sq2_fw_owner;
static const char image_name[] = "qcom/venus-5.2/venus.mbn";

static int sq2_fw_count_peer(struct device *dev, void *data)
{
 unsigned int *count = data;
 (*count)++;
 return 0;
}

static void sq2_fw_drop_stream(struct sq2_venus_fw *fw)
{
 if (fw->attached) iommu_detach_device(fw->domain, &fw->stream->dev);
 if (fw->domain) iommu_domain_free(fw->domain);
 if (fw->stream) platform_device_unregister(fw->stream);
 fw->attached = false;
 fw->domain = NULL;
 fw->stream = NULL;
}

static int sq2_fw_create_stream(struct sq2_venus_fw *fw)
{
 struct platform_device_info info = {0};
 struct device_node *np;
 struct of_phandle_args args;
 struct iommu_fwspec *spec;
 struct iommu_group *group;
 unsigned int peers = 0;
 int ret;

 np = of_get_child_by_name(fw->parent->of_node, "video-firmware");
 if (!np) return -ENODEV;
 ret = of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0, &args);
 if (ret) goto put;
 ret = args.args_count == 2 && args.args[0] == 0x2342 && !args.args[1] &&
       of_device_is_compatible(args.np, "qcom,sc8180x-smmu-500") &&
       of_property_count_u32_elems(np, "iommus") == 3 ? 0 : -EINVAL;
 of_node_put(args.np);
 if (ret) goto put;
 info.fwnode = of_fwnode_handle(np);
 info.parent = fw->parent;
 info.name = "sq2-venus-fw-stream";
 info.id = PLATFORM_DEVID_NONE;
 info.dma_mask = DMA_BIT_MASK(32);
 fw->stream = platform_device_register_full(&info);
 if (IS_ERR(fw->stream)) {
  ret = PTR_ERR(fw->stream);
  fw->stream = NULL;
  goto put;
 }
 ret = of_dma_configure(&fw->stream->dev, np, true);
 if (ret) goto fail;
 spec = dev_iommu_fwspec_get(&fw->stream->dev);
 if (!spec || spec->num_ids != 1 || spec->ids[0] != 0x2342) {
  ret = -EINVAL;
  goto fail;
 }
 group = iommu_group_get(&fw->stream->dev);
 if (!group) { ret = -ENODEV; goto fail; }
 ret = iommu_group_for_each_dev(group, &peers, sq2_fw_count_peer);
 iommu_group_put(group);
 if (ret || peers != 1) { ret = ret ? ret : -EBUSY; goto fail; }
 fw->domain = iommu_domain_alloc(&platform_bus_type);
 if (!fw->domain) { ret = -ENOMEM; goto fail; }
 ret = iommu_attach_device(fw->domain, &fw->stream->dev);
 if (ret) goto fail;
 fw->attached = true;
 pr_info("SQ2_NO_TZ stream_attached sid=2342 singleton=1\n");
 goto put;
fail:
 sq2_fw_drop_stream(fw);
put:
 of_node_put(np);
 return ret;
}

static int sq2_fw_no_tz_stop(struct sq2_venus_fw *fw)
{
 size_t unmapped;
 int ret;
 if (fw->cpu_touched) {
  ret = sq2_no_tz_stop_cpu(fw->no_tz_base);
  pr_info("SQ2_NO_TZ reset_assert ret=%d\n", ret);
  if (ret) return ret;
  fw->cpu_touched = false;
 }
 if (fw->mapped) {
  unmapped = iommu_unmap(fw->domain, 0, 0x500000);
  pr_info("SQ2_NO_TZ unmapped=%zu expected=5242880\n", unmapped);
  if (unmapped != 0x500000) return -EIO;
  fw->mapped = false;
 }
 return 0;
}

/* Fixed Surface source marks 8bd80000..8d800000 free for the image loader.
 * Claim only a 6MiB aligned subset. Query window alone is never sufficient.
 */
static int sq2_fw_claim_gap(struct sq2_venus_fw *fw)
{
 struct device_node *root, *np;
 struct resource r;
 phys_addr_t start, end;
 const phys_addr_t base = 0x8c000000, limit = base + SQ2_FW_LIMIT;
 int i, count, ret;
 ret = sq2_pil_get_bounds(&start, &end);
 if (ret) return ret;
 if (start != 0x8b700000 || end != 0x9ab00000 || base < start || limit > end)
  return -ERANGE;
 if (region_intersects(base, SQ2_FW_LIMIT, IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE) != REGION_DISJOINT)
  return -EBUSY;
 root = of_find_node_by_path("/reserved-memory");
 if (!root) return -EINVAL;
 for_each_child_of_node(root, np) {
  count = of_property_count_u32_elems(np, "reg");
  if (count <= 0 || count % 4) { ret = -EINVAL; goto node_fail; }
  for (i = 0; i < count / 4; i++) {
   if (of_address_to_resource(np, i, &r)) { ret = -EINVAL; goto node_fail; }
   if (r.start < limit && r.end >= base) { ret = -EBUSY; goto node_fail; }
  }
 }
 of_node_put(root);
 fw->region.start = base;
 fw->region.end = limit - 1;
 fw->region.name = "sq2-video-fw-lab";
 fw->region.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
 ret = insert_resource(&iomem_resource, &fw->region);
 if (ret) return ret;
 /* No existing owner may be wrapped or nested under. Require the exact
  * generic UEFI reserved window, not an arbitrary reserved block.
  */
 if (fw->region.child || !fw->region.parent ||
     strcmp(fw->region.parent->name, "reserved") ||
     fw->region.parent->start != start || fw->region.parent->end != end - 1) {
  remove_resource(&fw->region);
  return -EBUSY;
 }
 fw->cpu = memremap(base, SQ2_FW_LIMIT, MEMREMAP_WC);
 if (!fw->cpu) { remove_resource(&fw->region); return -ENOMEM; }
 fw->phys = base;
 pr_info("SQ2_VENUS_FW gap_claimed phys=%pa bytes=%u; DT/IORESOURCE owners excluded\n",
         &fw->phys, SQ2_FW_LIMIT);
 return 0;
node_fail:
 of_node_put(np);
 of_node_put(root);
 return ret;
}
static void sq2_fw_unmap_gap(struct sq2_venus_fw *fw)
{
 memunmap(fw->cpu);
 remove_resource(&fw->region);
 fw->cpu = NULL;
}
int sq2_venus_fw_probe_gap(void)
{
 struct sq2_venus_fw *fw;
 int ret;
 fw = kzalloc(sizeof(*fw), GFP_KERNEL);
 if (!fw) return -ENOMEM;
 ret = sq2_fw_claim_gap(fw);
 if (!ret) sq2_fw_unmap_gap(fw);
 pr_info("SQ2_VENUS_FW gap_probe ret=%d NO_MEMORY_WRITES_NO_LOAD\n", ret);
 kfree(fw);
 return ret;
}

static int sq2_fw_check_image(const struct firmware *fw)
{
 static const u8 expected[32] = {
  0x4e,0x93,0xd3,0x45,0x04,0xda,0x96,0x32,0x07,0x75,0x47,0x6b,0xbd,0x50,0x9a,0xa0,
  0x6f,0x98,0x5b,0x48,0xfb,0x78,0x1d,0x44,0x36,0xc8,0xeb,0x81,0x2b,0x92,0x99,0x28
 };
 struct sq2_fw_layout layout;
 struct crypto_shash *tfm;
 struct shash_desc *desc;
 u8 digest[32];
 int ret;

 if (fw->size != 1159200 || sq2_fw_layout(fw->data, fw->size, &layout) ||
     qcom_mdt_get_size(fw) != layout.span)
  return -ENOEXEC;
 tfm = crypto_alloc_shash("sha256", 0, 0);
 if (IS_ERR(tfm))
  return PTR_ERR(tfm);
 desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
 if (!desc) {
  ret = -ENOMEM;
  goto out;
 }
 desc->tfm = tfm;
 ret = crypto_shash_digest(desc, fw->data, fw->size, digest);
 if (!ret && memcmp(digest, expected, sizeof(digest)))
  ret = -EKEYREJECTED;
 kfree(desc);
out:
 crypto_free_shash(tfm);
 return ret;
}

static int sq2_fw_alloc(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 struct device *dev;
 int ret;

 fw->pdev = platform_device_register_simple("sq2-venus-fw-memory", -1, NULL, 0);
 if (IS_ERR(fw->pdev))
  return PTR_ERR(fw->pdev);
 dev = &fw->pdev->dev;
 if (iommu_get_domain_for_dev(dev) || get_dma_ops(dev) || dev->dma_range_map) {
  ret = -EINVAL;
  goto unregister;
 }
 ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret)
  goto unregister;
 if (fw->pil_gap) {
  ret = sq2_fw_claim_gap(fw);
  if (ret) goto unregister;
  goto pin;
 }
 fw->cpu = dma_alloc_coherent(dev, SQ2_FW_LIMIT, &fw->dma, GFP_KERNEL | __GFP_NOWARN);
 if (!fw->cpu) {
  ret = -ENOMEM;
  goto unregister;
 }
 fw->phys = dma_to_phys(dev, fw->dma);
 if (fw->phys != fw->dma || !IS_ALIGNED(fw->phys, SZ_1M) ||
     fw->phys + SQ2_FW_LIMIT > (1ULL << 32)) {
  ret = -ERANGE;
  goto free;
 }
pin:
 /* A quarantine must keep the module and auxiliary DMA device alive too. */
 if (!try_module_get(THIS_MODULE)) {
  ret = -ENODEV;
  goto free;
 }
 memset(fw->cpu, 0, SQ2_FW_LIMIT);
 pr_info("SQ2_VENUS_FW phase=allocated phys=%pa bytes=%u\n", &fw->phys, SQ2_FW_LIMIT);
 return 0;
free:
 if (fw->pil_gap) sq2_fw_unmap_gap(fw);
 else dma_free_coherent(dev, SQ2_FW_LIMIT, fw->cpu, fw->dma);
 fw->cpu = NULL;
unregister:
 platform_device_unregister(fw->pdev);
 fw->pdev = NULL;
 return ret;
}
static int sq2_fw_no_tz_load(struct sq2_venus_fw *fw)
{
 u32 off;
 int ret = sq2_fw_create_stream(fw);
 if (ret) return ret;
 ret = qcom_mdt_load_no_init(&fw->pdev->dev, fw->image, image_name, 9,
                           fw->cpu, fw->phys, SQ2_FW_LIMIT, NULL);
 if (ret) return ret;
 wmb();
 ret = iommu_map(fw->domain, 0, fw->phys, 0x500000,
                 IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
 if (ret) return ret;
 fw->mapped = true;
 for (off = 0; off < 0x500000; off += PAGE_SIZE)
  if (iommu_iova_to_phys(fw->domain, off) != fw->phys + off) return -EFAULT;
 pr_info("SQ2_NO_TZ mapped sid=2342 iova=0 phys=%pa bytes=5242880; all_pages_checked\n", &fw->phys);
 return 0;
}
static int sq2_fw_load(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 const __be32 *prepared;
 int ret, len;
 pr_info("SQ2_VENUS_FW phase=mdt_load_begin\n");
 if (fw->no_tz_base) return sq2_fw_no_tz_load(fw);
 prepared = of_get_property(of_root, "microsoft,el2-venus-prepared", &len);
 if (prepared) {
  /* Only the checked EFI metadata handoff may skip Linux PAS init. */
  if (len != 16 || be32_to_cpup(prepared) != 1 ||
      be32_to_cpup(prepared + 1) != 9 ||
      be32_to_cpup(prepared + 2) != 0x8c000000 ||
      be32_to_cpup(prepared + 3) != 0x500000 ||
      !fw->pil_gap || fw->phys != 0x8c000000 ||
      !of_machine_is_compatible("qcom,sc8180x") ||
      qcom_mdt_get_size(fw->image) != 0x500000)
   return -EINVAL;
  pr_info("SQ2_VENUS_FW phase=efi_prepared_no_init\n");
  ret = qcom_mdt_load_no_init(&fw->pdev->dev, fw->image, image_name, 9,
                    fw->cpu, fw->phys, SQ2_FW_LIMIT, NULL);
 } else {
  ret = qcom_mdt_load(&fw->pdev->dev, fw->image, image_name, 9,
                    fw->cpu, fw->phys, SQ2_FW_LIMIT, NULL);
 }
 if (!ret) wmb(); /* Complete WC segment writes before secure authentication. */
 pr_info("SQ2_VENUS_FW phase=mdt_load_end ret=%d\n", ret);
 return ret;
}
static int sq2_fw_auth(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 int ret;
 if (fw->no_tz_base) {
  pr_info("SQ2_NO_TZ cpu_start_begin NO_PAS\n");
  msleep(100); /* Persist the boundary before the first unproven MMIO write. */
  fw->cpu_touched = true;
  ret = sq2_no_tz_start_cpu(fw->no_tz_base, 0x500000);
  pr_info("SQ2_NO_TZ cpu_start_end ret=%d\n", ret);
  return ret;
 }
 pr_info("SQ2_VENUS_FW phase=auth_begin\n");
 ret = qcom_scm_pas_auth_and_reset(9);
 pr_info("SQ2_VENUS_FW phase=auth_end ret=%d\n", ret);
 return ret;
}
static int sq2_fw_pas_stop(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 int ret;
 if (fw->no_tz_base) return sq2_fw_no_tz_stop(fw);
 pr_info("SQ2_VENUS_FW phase=pas_shutdown_begin\n");
 ret = qcom_scm_pas_shutdown(9);
 pr_info("SQ2_VENUS_FW phase=pas_shutdown_end ret=%d\n", ret);
 return ret;
}
static void sq2_fw_release(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 sq2_fw_drop_stream(fw);
 memzero_explicit(fw->cpu, SQ2_FW_LIMIT);
 wmb();
 if (fw->pil_gap) sq2_fw_unmap_gap(fw);
 else dma_free_coherent(&fw->pdev->dev, SQ2_FW_LIMIT, fw->cpu, fw->dma);
 platform_device_unregister(fw->pdev);
 fw->cpu = NULL;
 fw->pdev = NULL;
 module_put(THIS_MODULE);
 pr_info("SQ2_VENUS_FW phase=memory_released\n");
}
static const struct sq2_fw_operations sq2_fw_ops = {
 .alloc = sq2_fw_alloc, .load = sq2_fw_load, .start = sq2_fw_auth,
 .stop = sq2_fw_pas_stop, .release = sq2_fw_release,
};

int sq2_venus_fw_boot(struct device *dev, struct sq2_venus_fw **out, bool pil_gap,
                     void __iomem *no_tz_base)
{
 struct sq2_venus_fw *fw;
 bool supported;
 int ret;
 if (!out || *out || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EINVAL;
 if (no_tz_base) {
  if (!pil_gap || !is_kernel_in_hyp_mode() ||
      !of_machine_is_compatible("qcom,sc8180x")) return -EPERM;
  ret = sq2_no_tz_reset_held(no_tz_base);
  if (ret) return ret;
 } else {
 if (!qcom_scm_is_available())
  return -EPROBE_DEFER;
 supported = qcom_scm_pas_supported(9);
 pr_info("SQ2_VENUS_FW phase=support_query pas9=%d adsp1=%d cdsp18=%d mpss4=%d\n",
         supported, qcom_scm_pas_supported(1), qcom_scm_pas_supported(18),
         qcom_scm_pas_supported(4));
 }
 /* Advisory only: on this Surface all four queries report false although
  * PAS-backed ADSP/CDSP/MPSS are running. Like stock Venus, rely on the real
  * load/auth return values; never convert unsupported/error to success.
  */
 mutex_lock(&sq2_fw_lock);
 if (sq2_fw_owner) {
  ret = -EBUSY;
  goto unlock;
 }
 fw = kzalloc(sizeof(*fw), GFP_KERNEL);
 if (!fw) {
  ret = -ENOMEM;
  goto unlock;
 }
 fw->pil_gap = pil_gap;
 fw->no_tz_base = no_tz_base;
 fw->parent = dev;
 ret = request_firmware_direct(&fw->image, image_name, dev);
 if (ret)
  goto free;
 ret = sq2_fw_check_image(fw->image);
 if (!ret)
  ret = sq2_fw_start(&fw->tx, &sq2_fw_ops, fw);
 release_firmware(fw->image);
 fw->image = NULL;
 if (fw->tx.state != SQ2_FW_EMPTY) {
  sq2_fw_owner = fw;
  *out = fw;
  if (fw->tx.state == SQ2_FW_QUARANTINED)
   pr_err("SQ2_VENUS_FW shutdown=%d; DMA memory/device/module pinned until reboot\n",
          fw->tx.stop_error);
  goto unlock;
 }
free:
 kfree(fw);
unlock:
 mutex_unlock(&sq2_fw_lock);
 return ret;
}

int sq2_venus_fw_shutdown(struct sq2_venus_fw **owner)
{
 struct sq2_venus_fw *fw;
 int ret = 0;
 if (!owner)
  return -EINVAL;
 mutex_lock(&sq2_fw_lock);
 fw = *owner;
 if (!fw)
  goto unlock;
 if (fw != sq2_fw_owner) {
  ret = -EINVAL;
  goto unlock;
 }
 ret = sq2_fw_stop(&fw->tx, &sq2_fw_ops, fw);
 if (!ret) {
  sq2_fw_owner = NULL;
  *owner = NULL;
  kfree(fw);
 }
unlock:
 mutex_unlock(&sq2_fw_lock);
 return ret;
}
