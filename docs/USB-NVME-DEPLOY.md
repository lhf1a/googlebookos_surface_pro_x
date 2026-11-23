# USB–NVMe Adapter Deployment

Goal: connect the NVMe SSD destined for the Surface Pro X via a USB–NVMe enclosure, write the disk from another host, and reinstall it into the Surface.

## Environment and Entry Point

The current entry point is `tools/deploy-experimental-usb.py`, run on native Linux or a Linux VM with exclusive USB enclosure passthrough. Windows drive letters in standard WSL are not block devices and cannot be targeted.

## Deployment Flow

1. The script lists unmounted, writable USB disks with serial numbers and 512-byte logical sectors, rejecting workstation internal NVMe, mounted disks, read-only disks, disks without serial numbers, and disks with insufficient capacity.
2. Pass `--manifest MANIFEST --layout LAYOUT --image IMAGE` from the same prepared image. After selecting the disk index, it shows a preview; pass `--execute` and enter the wipe confirmation containing the target serial number to begin writing.
3. Before writing, all source extents in the manifest are verified; the image's GPT, ESP, and Android partition layout is written and verified extent by extent on readback.
4. The backup GPT is moved to the end of the disk, the physically last `userdata` partition is expanded, and ext4 is grown with `e2fsck`/`resize2fs`, preserving partition start, GUID, type, and filesystem UUID without reformatting `data`.

## Limitations and Build Configuration

- Target disk capacity must be at least the logical size of the input image; smaller SSDs require generating a matching base image first.
- When changing target capacity, capacity/partition records in the boot image must be updated to match, otherwise boot protection rejects the boot.
- Ghost-touch toggles and default Qualcomm / optional Turnip menus must be configured during input-image build; `build-source-boot.py` currently generates builds with both touch patches disabled, `build-test3-iptsd.py` still supports `on`/`off`, and touch 0996 adaptation is documented in `TOUCH-DEVICE-COMPAT.md`.
