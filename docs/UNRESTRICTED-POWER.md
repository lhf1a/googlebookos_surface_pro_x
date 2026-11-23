> Legacy diagnostic tools `lte-fs12-seed`, `lte-fsg-seed`, and `el2-preboot-init` have been retired; see [SUSPEND](SUSPEND.md) for the current implementation.

# screen_only Dependency Cleanup and Unrestricted Environment

## Mechanism and Scope

- `scripts/build.py` and `build-surface-pro-x.py` default to the `unrestricted` policy, explicitly passing `screen_only=0` and `screen_only_autosuspend=0`.
- Legacy kernel owner/one-shot/deadline gating no longer participates in admission, `init` skips owner registration and keep-alive, and manual `PM_DEBUG` phase tests are not wired into normal boot.
- Normal modules no longer require `screen_only=Y`, covering LTE/Radio, Video Codec2 and sampling tools, USB, KSU, swap, EL2/AVF/Terminal, graphics probes, CPU/GPU test entries, and display/GPU packaging checks.
- Real hardware/ABI/firmware-hash/disk-boundary/thermal checks, credentials, user settings, standard Android wakelocks, `KEEP_SCREEN_ON`, `offscreen_only`, `nohibernate`, and the original Mesa fallback remain intact.

## Codec2 and Environment Build

- `probe/video/sq2-c2-runtime-launch.c` is compiled with Android NDK API 35 using `-DBASE='"/data/vendor/sq2-codec2/v2u/"'` and passed as `--codec-launcher PATH`; it retains the private mount namespace, mapper, and `MAP_CACHE` contract while preserving and verifying `v1`.
- If an older package has the fixed `VIDEOCC` b5 gate, pass `--video-reference-module PATH` using the dynamic reference module from `build-sq2-hfi-overlay.py`; packaging retains compatible/cell-width/ABI/hash/firmware checks and rejects missing inputs.
- `scripts/prepare-unrestricted-environment.py` builds the incremental environment offline using the operator's own baseline, private identity, and matching kernel/module bundle. The new EFI loads via the original initrd + overlay, keeping the final cpio entries consistent with the identity member last.

## Original Referenced File Classification

| File | Category | Handling |
|---|---|---|
| `configs/deployment-status.json`, `configs/sq2-video-hfi-status.json` | Historical state | Keep old observations |
| `configs/grub-internal-ssd.cfg`, `configs/grub-source.cfg`, `configs/suspend-known-issues.json` | Feature/build entry | Removed mandatory Y |
| `patches/sq1-suspend-wip/*`, `patches/surface-pm/*`, `patches/suspend-el2-trial/*` | Historical/compat kernel patches | Kept for rollback repro; N bypasses all gates |
| `probe/graphics/vulkan-readback.c` | `offscreen_only` | Keep offscreen rendering mode |
| `probe/graphics/quest2-*`, `probe/lte/*`, `probe/test2/*`, `probe/video/*`, `probe/virtualization/*` | Feature/build entry | Removed mandatory Y |
| `scripts/check-sq1-autosuspend-policy.py`, `scripts/check-sq1-suspend-wip.py`, `scripts/check-surface-native-once.py`, `scripts/check-suspend-trial-policy.py`, `scripts/patch-sq2-screen-only.py`, `scripts/prepare-surface-pm-debug.py`, `scripts/smoke-sq2-ksunext.py`, `scripts/smoke-sq2-screen-only.py` | Legacy diagnostic/status tools | Not wired into new entry; kept for rollback verification |
| Remaining `scripts/*` feature and packaging scripts | Feature/build entry | Removed mandatory Y |
| `tests/*` | Tests | Keep compat/integrity checks; updated N-success expectations and rejection scenarios |
