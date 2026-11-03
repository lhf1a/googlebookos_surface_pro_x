// SPDX-License-Identifier: GPL-2.0-only
/* Manual SQ2 EL2 lab export of the current firmware IORT, even with acpi=off.
 * Copies only checksum-validated ACPI RAM tables, never calls ACPI/EFI runtime
 * services or accesses MMIO, clocks, IOMMU registers, DMA, MPSS, NV or disks.
 * All transient mappings are released in init; one immutable IORT copy remains
 * behind a root-read-only sysfs file until rmmod. No arbitrary address parameter.
 */
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <asm/virt.h>
#include "sq2-ipa-acpi.h"

static bool allow;
module_param(allow, bool, 0400);
static unsigned long rsdp_phys, xsdt_phys, iort_phys;
module_param(rsdp_phys, ulong, 0444);
module_param(xsdt_phys, ulong, 0444);
module_param(iort_phys, ulong, 0444);
static unsigned int iort_bytes, xsdt_entries, rejected_entries;
module_param(iort_bytes, uint, 0444);
module_param(xsdt_entries, uint, 0444);
module_param(rejected_entries, uint, 0444);
static void *iort;
static struct kobject *directory;

static int acpi_ram_range(u64 addr, size_t size)
{
 efi_memory_desc_t *md;
 unsigned int i;
 u64 end;
 if (!size || size > SQ2_ACPI_TABLE_MAX || addr < 0x80000000ULL ||
     addr > U64_MAX - size) return -EINVAL;
 end = addr + size;
 if (!efi_enabled(EFI_MEMMAP) || !efi.memmap.map ||
     efi.memmap.nr_map <= 0 || efi.memmap.nr_map > 4096 ||
     efi.memmap.desc_size < sizeof(*md) || efi.memmap.desc_size > 256 ||
     (unsigned long)efi.memmap.map_end < (unsigned long)efi.memmap.map ||
     (unsigned long)efi.memmap.map_end - (unsigned long)efi.memmap.map !=
         efi.memmap.nr_map * efi.memmap.desc_size) return -EOPNOTSUPP;
 for (i = 0; i < efi.memmap.nr_map; i++) {
  md = efi.memmap.map + i * efi.memmap.desc_size;
  if (md->num_pages > (U64_MAX >> EFI_PAGE_SHIFT) ||
      md->phys_addr > U64_MAX - (md->num_pages << EFI_PAGE_SHIFT)) continue;
  if (addr < md->phys_addr || end > md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT)) continue;
  if ((md->type != EFI_ACPI_RECLAIM_MEMORY && md->type != EFI_ACPI_MEMORY_NVS) ||
      !(md->attribute & EFI_MEMORY_WB)) return -EPERM;
  return 0;
 }
 return -ERANGE;
}

static int copy_acpi_ram(u64 phys, void *dst, size_t size)
{
 void *map;
 int ret = acpi_ram_range(phys, size);
 if (ret) return ret;
 map = memremap(phys, size, MEMREMAP_WB);
 if (!map) return -ENOMEM;
 memcpy(dst, map, size);
 memunmap(map);
 return 0;
}

static ssize_t read_iort(struct file *file, struct kobject *kobj,
                        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
 if (off < 0) return -EINVAL;
 if (off >= iort_bytes) return 0;
 count = min_t(size_t, count, iort_bytes - off);
 memcpy(buf, iort + off, count);
 return count;
}
static struct bin_attribute iort_file = {
 .attr = { .name = "IORT", .mode = 0400 },
 .read = read_iort,
};

static int __init start(void)
{
 unsigned char rsdp[36], header[36], *xsdt = NULL;
 unsigned long long addr;
 unsigned int len, i;
 int ret;
 if (!allow || !is_kernel_in_hyp_mode() ||
     !of_machine_is_compatible("microsoft,surface-pro-x") ||
     !of_machine_is_compatible("qcom,sc8180x")) {
  pr_info("SQ2_IPA_IORT_REFUSED: explicit SQ2 EL2 allow required; no physical read\n");
  return -EPERM;
 }
 if (!efi_enabled(EFI_BOOT) || !efi.acpi20 || efi.acpi20 == EFI_INVALID_TABLE_ADDR) return -ENODEV;
 rsdp_phys = efi.acpi20;
 pr_info("SQ2_IPA_IORT start rsdp=%lx efi_map_count=%d desc_size=%lu\n",
         rsdp_phys, efi.memmap.nr_map, efi.memmap.desc_size);
 ret = copy_acpi_ram(rsdp_phys, rsdp, sizeof(rsdp));
 if (ret) goto done;
 if (sq2_acpi_rsdp(rsdp, sizeof(rsdp), &addr)) { ret = -EINVAL; goto done; }
 xsdt_phys = addr;
 ret = copy_acpi_ram(addr, header, sizeof(header));
 if (ret) goto done;
 len = sq2_acpi_length(header, sizeof(header), "XSDT", SQ2_ACPI_XSDT_MAX);
 if (!len || (len - 36) % 8) { ret = -EINVAL; goto done; }
 xsdt = kmalloc(len, GFP_KERNEL);
 if (!xsdt) { ret = -ENOMEM; goto done; }
 ret = copy_acpi_ram(addr, xsdt, len);
 if (ret) goto done;
 if (!sq2_acpi_checksum(xsdt, len)) { ret = -EBADMSG; goto done; }
 xsdt_entries = (len - 36) / 8;
 for (i = 0; i < xsdt_entries; i++) {
  addr = sq2_acpi_u64(xsdt + 36 + 8 * i);
  ret = copy_acpi_ram(addr, header, sizeof(header));
  if (ret) { rejected_entries++; continue; }
  if (memcmp(header, "IORT", 4)) continue;
  if (iort) { ret = -EEXIST; goto done; }
  iort_bytes = sq2_acpi_length(header, sizeof(header), "IORT", SQ2_ACPI_TABLE_MAX);
  if (iort_bytes < 48) { ret = -EINVAL; goto done; }
  iort = kmalloc(iort_bytes, GFP_KERNEL);
  if (!iort) { ret = -ENOMEM; goto done; }
  ret = copy_acpi_ram(addr, iort, iort_bytes);
  if (ret) goto done;
  if (!sq2_acpi_checksum(iort, iort_bytes)) { ret = -EBADMSG; goto done; }
  iort_phys = addr;
 }
 if (!iort) { ret = -ENOENT; goto done; }
 directory = kobject_create_and_add("sq2_ipa_iort", kernel_kobj);
 if (!directory) { ret = -ENOMEM; goto done; }
 iort_file.size = iort_bytes;
 ret = sysfs_create_bin_file(directory, &iort_file);
 if (ret) { kobject_put(directory); directory = NULL; goto done; }
 pr_info("SQ2_IPA_IORT captured phys=%lx bytes=%u xsdt_entries=%u rejected=%u CHECKSUM_VALID NO_WRITES\n",
         iort_phys, iort_bytes, xsdt_entries, rejected_entries);
 ret = 0;
done:
 kfree(xsdt);
 if (ret) { kfree(iort); iort = NULL; pr_info("SQ2_IPA_IORT stopped ret=%d NO_WRITES\n", ret); }
 return ret;
}
static void __exit stop(void)
{
 sysfs_remove_bin_file(directory, &iort_file);
 kobject_put(directory);
 kfree(iort);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Manual bounded current SQ2 firmware IORT RAM-only export");
