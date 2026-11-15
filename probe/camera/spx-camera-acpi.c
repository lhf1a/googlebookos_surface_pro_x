// SPDX-License-Identifier: GPL-2.0-only
/* Manual Surface Pro X EL2 lab export of the current firmware DSDT/IORT via XSDT/FADT, even with acpi=off.
 * Copies only checksum-validated ACPI RAM tables, never calls ACPI/EFI runtime
 * services or accesses MMIO, clocks, IOMMU registers, DMA, MPSS, NV or disks.
 * All transient mappings are released in init; immutable table copies remain
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
#include "spx-camera-acpi.h"

static bool allow;
module_param(allow, bool, 0400);
static unsigned long rsdp_phys, xsdt_phys, dsdt_phys;
module_param(rsdp_phys, ulong, 0444);
module_param(xsdt_phys, ulong, 0444);
module_param(dsdt_phys, ulong, 0444);
static unsigned int dsdt_bytes, xsdt_entries, rejected_entries;
module_param(dsdt_bytes, uint, 0444);
module_param(xsdt_entries, uint, 0444);
module_param(rejected_entries, uint, 0444);
static void *dsdt, *iort;
static unsigned int iort_bytes;
static struct kobject *directory;

static int acpi_ram_range(u64 addr, size_t size)
{
 efi_memory_desc_t *md;
 unsigned int i;
 u64 end;
 if (!size || size > SPX_CAMERA_ACPI_MAX || addr < 0x80000000ULL ||
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

static ssize_t read_dsdt(struct file *file, struct kobject *kobj,
                        const struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
 if (off < 0) return -EINVAL;
 if (off >= dsdt_bytes) return 0;
 count = min_t(size_t, count, dsdt_bytes - off);
 memcpy(buf, dsdt + off, count);
 return count;
}
static struct bin_attribute dsdt_file = {
 .attr = { .name = "DSDT", .mode = 0400 },
 .read = read_dsdt,
};

static ssize_t read_iort(struct file *file, struct kobject *kobj,
                        const struct bin_attribute *attr, char *buf, loff_t off, size_t count)
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
  pr_info("SPX_CAMERA_ACPI_REFUSED: explicit Surface Pro X EL2 allow required; no physical read\n");
  return -EPERM;
 }
 if (!efi_enabled(EFI_BOOT) || !efi.acpi20 || efi.acpi20 == EFI_INVALID_TABLE_ADDR) return -ENODEV;
 rsdp_phys = efi.acpi20;
 pr_info("SPX_CAMERA_ACPI start rsdp=%lx efi_map_count=%d desc_size=%lu\n",
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
  if (!memcmp(header, "IORT", 4)) {
   if (iort) { ret = -EEXIST; goto done; }
   iort_bytes = sq2_acpi_length(header, sizeof(header), "IORT", SPX_CAMERA_ACPI_MAX);
   if (!iort_bytes) { ret = -EINVAL; goto done; }
   iort = kmalloc(iort_bytes, GFP_KERNEL);
   if (!iort) { ret = -ENOMEM; goto done; }
   ret = copy_acpi_ram(addr, iort, iort_bytes);
   if (ret) goto done;
   if (!sq2_acpi_checksum(iort, iort_bytes)) { ret = -EBADMSG; goto done; }
   continue;
  }
  if (memcmp(header, "FACP", 4)) continue;
  {
   unsigned char *fadt;
   unsigned int fadt_len = sq2_acpi_length(header, sizeof(header), "FACP", SQ2_ACPI_TABLE_MAX);
   if (!fadt_len) { ret = -EINVAL; goto done; }
   fadt = kmalloc(fadt_len, GFP_KERNEL);
   if (!fadt) { ret = -ENOMEM; goto done; }
   ret = copy_acpi_ram(addr, fadt, fadt_len);
   if (!ret && spx_camera_dsdt_address(fadt, fadt_len, &addr))
    ret = -EBADMSG;
   kfree(fadt);
   if (ret) goto done;
  }
  ret = copy_acpi_ram(addr, header, sizeof(header));
  if (ret) goto done;
  if (dsdt) { ret = -EEXIST; goto done; }
  dsdt_bytes = sq2_acpi_length(header, sizeof(header), "DSDT", SPX_CAMERA_ACPI_MAX);
  if (!dsdt_bytes) { ret = -EINVAL; goto done; }
  dsdt = kmalloc(dsdt_bytes, GFP_KERNEL);
  if (!dsdt) { ret = -ENOMEM; goto done; }
  ret = copy_acpi_ram(addr, dsdt, dsdt_bytes);
  if (ret) goto done;
  if (!sq2_acpi_checksum(dsdt, dsdt_bytes)) { ret = -EBADMSG; goto done; }
  dsdt_phys = addr;
 }
 if (!dsdt) { ret = -ENOENT; goto done; }
 directory = kobject_create_and_add("spx_camera_acpi", kernel_kobj);
 if (!directory) { ret = -ENOMEM; goto done; }
 dsdt_file.size = dsdt_bytes;
 ret = sysfs_create_bin_file(directory, &dsdt_file);
 if (ret) { kobject_put(directory); directory = NULL; goto done; }
 if (iort) {
  iort_file.size = iort_bytes;
  ret = sysfs_create_bin_file(directory, &iort_file);
  if (ret) {
   sysfs_remove_bin_file(directory, &dsdt_file);
   kobject_put(directory); directory = NULL;
   goto done;
  }
  pr_info("SPX_CAMERA_ACPI IORT bytes=%u CHECKSUM_VALID NO_WRITES\n", iort_bytes);
 }
 pr_info("SPX_CAMERA_ACPI captured phys=%lx bytes=%u xsdt_entries=%u rejected=%u CHECKSUM_VALID NO_WRITES\n",
         dsdt_phys, dsdt_bytes, xsdt_entries, rejected_entries);
 ret = 0;
done:
 kfree(xsdt);
 if (ret) { kfree(dsdt); dsdt = NULL; kfree(iort); iort = NULL; pr_info("SPX_CAMERA_ACPI stopped ret=%d NO_WRITES\n", ret); }
 return ret;
}
static void __exit stop(void)
{
 if (iort) sysfs_remove_bin_file(directory, &iort_file);
 sysfs_remove_bin_file(directory, &dsdt_file);
 kobject_put(directory);
 kfree(dsdt);
 kfree(iort);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Manual Surface Pro X camera ACPI DSDT/IORT RAM-only export");
