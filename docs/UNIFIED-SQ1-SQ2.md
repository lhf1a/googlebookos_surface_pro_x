> The sole current build entry point has switched to 6.18 LTS; see [BUILD](BUILD.md). The default profile includes the LTE/Radio delta while Mesa fallback remains non-LTE; see [LTE Current Path](LTE-PRODUCT.md). The 6.3.8 command below is kept for historical reference.

# SQ1 / SQ2 Unified Build

## Platform Inputs and Decisions

| Input | Observation | Decision |
| --- | --- | --- |
| ESP | Windows FAT UUID `2010-259C`, 256GB GPT | SQ2's `B027-0512` cannot locate it; do not write Windows ESP into Android installer |
| Android layout | No Android partitions on queried SQ1 Windows disk | SQ1 build targets the offline 20GiB Android image template `B026-1027` |
| Platform | SC8180X family; IORT `acfe9033…` matches SQ2 | Share platform implementation |
| TCB | `195997cf…`, matches pinned TCB | Reuse pinned input |
| MPSS / CDSP | `9ec7b6c6…` / `2a276b79…` match Linux pins | Reuse pinned blobs |
| ADSP | Windows `09c06fe8…` spans 28MiB vs 26MiB DT carveout | Retain fitting Linux `2703ceb6…` blob |
| Video | Windows `891997ec…` differs from Linux `4e93d345…` | Keep Linux42 firmware and no-TZ runtime |
| Firmware version | SQ1 UEFI `4.234.140` | Recorded for reference |

The original SQ2 DT is unchanged. The SQ1 derived candidate modifies only the model label and frequency policy: removes GPU700 and the two >3GHz CPU OPPs, using GPU661/RPMh416 and big CPU max 2995.2MHz. Reserved memory, firmware names, SMMU/interrupt/topology data, and thermal trips remain identical.

## Build Entry Point and Shared Baseline

```sh
python3 scripts/build-source-boot.py --machine both \
  --sealed-input private/unified-input --output out/unified-boot
```

`--machine sq1` and `--machine sq2` select a single profile, defaulting to SQ2. `--rebuild-components` forwards to the mainline component-rebuild backend; `--legacy-el1` is unchanged.

Both profiles share initramfs `957b5ee6…` and Image `c1ef559f…`, including all USB modules, USB input, Wi-Fi/BT policies, FUSE, screen-only, audio `92852a7b…` + WSA881x `b165ca2c…`, HAL `774cd7e7…`, no-TZ video, KSU/KVM/AVF preparation, manual Terminal, thermal/battery/touch fixes, and Qualcomm Vulkan / Mesa options. SQ1 keeps the same kernel/module ABI without LTE lab drivers or experimental DT.

The SQ1 donor is hash-pinned and used only for its Android-image storage/growth implementation and lab SSH identity, retained in the final credential-only member. SQ2 keeps `user10` autologin; SQ1 does not assume that account exists. Each profile produces default Qualcomm Vulkan and Mesa fallback entries with `sq2.el2=1`, native 60Hz, and screen-only/nohibernate; SQ1 launcher copies receive equal-length directory-token changes only.

## Outputs

Each output contains `SQ1/` and/or `SQ2/`, an EFI payload pair, pinned firmware, `grub.cfg`, `SHA256SUMS`, and build audit. Artifacts contain private firmware and lab identities and must remain in ignored `private/` / `out/`; the build does not modify existing 20GiB whole-disk images or replace shared GRUB firmware.
