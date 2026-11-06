> 2026-11-07：当前唯一编译入口已切换6.18 LTS，见 [BUILD](BUILD.md)。下方旧6.3.8构建命令和默认档位仅作历史；当前私有验收包保持Mesa/GPU700，通用交付与跨机验收边界不变。Scene Root已确认，GPU应用读数仍待适配。

> 2026-11-03 更新：默认项已接入 LTE/Radio 增量，Mesa 回退保持非 LTE；仍为一个通用镜像。
> 私有输入、默认 on/off 与单机激活边界以 [LTE 默认基线](LTE-BASELINE.md) 为准。

# SQ1 / SQ2 unified build — 2026-11-02

## Scope and offline decision

This worktree starts at freshly fetched `origin/main`
`fee67b2dc1ce0de100e38131412c3f9062ce11fe`. The LTE research worktree is not
merged, reset or stashed. No physical disk was written, no firmware/driver was
loaded and no hardware, VM or functional test was run for this change.

Read-only queries against the identified ARM64 SQ1 Windows 22631 machine found:

| Input | Observation | Decision |
| --- | --- | --- |
| ESP | Windows FAT serial/UUID `2010-259C`, Windows-only 256GB GPT | SQ2's `B027-0512` cannot locate it; never substitute the Windows ESP into an Android installer |
| Android layout | No Android partitions in the queried SQ1 Windows disk | SQ1 build targets the already-created offline 20GiB Android image template (`B026-1027`), not this Windows disk |
| Platform | SC8180X family; saved IORT `acfe9033…` matches the SQ2 IORT | Supports a shared platform implementation, not proof that the entire DT works |
| TCB | `195997cf…`, equal to the pinned TCB | Reuse the pinned input; firmware acceptance remains untested on SQ1 |
| MPSS / CDSP | `9ec7b6c6…` / one stored CDSP `2a276b79…` match Linux pins | Reuse the fixed blobs; a second CDSP version in DriverStore is not silently selected |
| ADSP | SQ1 Windows `09c06fe8…` equals the archived SQ2 Windows blob | Both differ from selected Linux `2703ceb6…`; Windows ELF span is 28MiB, DT carveout is 26MiB; retain the fitting Linux blob |
| Video | Windows `891997ec…` differs from accepted Linux `4e93d345…` | Keep the accepted Linux42 firmware/no-TZ runtime; do not replace it with Windows79 |
| Firmware version | SQ1 UEFI `4.234.140`, not the SQ2 firmware version | EL2/TCB takeover cannot be certified by offline matching alone |

Thus **merge the build/runtime, not disk identities or hardware-validation
claims**. The original SQ2 DT is unchanged. SQ1's derived candidate changes only
the model label and frequency policy: remove GPU700 and the two >3GHz CPU OPPs,
use GPU661/RPMh416 and big CPU max2995.2MHz. All reserved memory, firmware names,
SMMU/interrupt/topology data, thermal trips and remaining DT properties stay
identical. These conservative caps are not newly measured SQ1 performance.

## One entry point, shared capability baseline

```sh
python3 scripts/build-source-boot.py --machine both \
  --sealed-input private/unified-input --output out/unified-boot
```

`--machine sq1` and `--machine sq2` select one profile. Default is SQ2. This is
deterministic assembly of pinned accepted binaries, **not a new kernel build**.
`--rebuild-components` forwards to the existing mainline SQ2 component-rebuild
backend; its original flags and hash gates remain. `--legacy-el1` is unchanged.
Neither path downloads secrets or automatically connects to any board.

Both profiles start from initramfs `957b5ee6…` and Image `c1ef559f…`, including
all USB modules/dependencies, USB input, Wi-Fi/BT policies, FUSE, screen-only,
audio `92852a7b…` + original WSA881x `b165ca2c…`, HAL `774cd7e7…`, no-TZ video,
KSU/KVM/AVF preparation, manual Terminal, thermal/battery/touch fixes and both
Qualcomm Vulkan / Mesa choices. SQ1 keeps the same kernel/module ABI, not the
older SQ1 donor kernel. No LTE lab driver/experimental DT is integrated.

The SQ1 donor is hash-pinned and used **only** for its existing Android-image
storage/growth implementation and own lab SSH identity. Effective identity
bytes are retained in a final credential-only member; no SQ2 identity is hidden
in earlier members. The current SQ2's existing user10 autologin is kept; SQ1
does not assume that account exists, does not create it or clear credentials.
AVF/network preparation and manual Terminal remain. Common runtime entries
outside the explicit storage/identity/login delta must be byte-identical.

Each profile produces exactly two entries: default Qualcomm Vulkan and Mesa
fallback, normal `sq2.el2=1`, native 60Hz and screen-only/nohibernate. The `sq2.*`
runtime names and firmware subdirectory remain shared compatibility interfaces,
not a claim that SQ1 is a different SoC. Unsigned, pinned launcher copies receive
equal-length directory-token changes only for SQ1; they are not called newly
compiled or newly signed firmware. SQ2 launcher/DT/payload/menu bytes are retained.

## Outputs and limitations

Each output has `SQ1/` and/or `SQ2/`, an EFI payload pair, pinned firmware,
`grub.cfg`, `SHA256SUMS`, and machine-readable build audit. The independent
readback audit compares all shared effective entries, both directory manifests,
firmware, DT property delta and the final identity member. Original files are
not overwritten. The new boot bundles do not modify/rebuild the existing 20GiB
whole-disk image and are not ready to write over the SQ1 Windows installation.

These artifacts are private: they contain firmware and lab identities and must
stay in ignored `private/` / `out/`. Shared GRUB firmware is not replaced.
SQ2 fallback is assembled with the same full runtime as the new default; the
deployed older Mesa fallback is untouched. No deployment or reboot was requested.

**SQ1 capability parity here means packaged implementation parity, not observed
hardware functionality.** Full hardware acceptance, acoustic performance,
EL2 handoff and all-device compatibility remain unverified by explicit request.
