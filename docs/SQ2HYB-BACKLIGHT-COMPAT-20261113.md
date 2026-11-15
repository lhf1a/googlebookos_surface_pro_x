# SQ2Hyb Userspace Backlight Compatibility (`panel0-backlight` -> `backlight`)

## Root Cause (Observed on Live Device)

1. Android Framework (`DisplayPowerController`) delegates built-in display backlight control to SurfaceControl/HWC (`BacklightAdapter [useSurfaceControl=true, backlight=null]`) rather than `android.hardware.lights-service.android-desktop`, because QTI `HWCSession::GetDisplayBrightnessSupport()` reports built-in panel brightness support as `true`.
2. Inside `/vendor/bin/hw/vendor.qti.hardware.display.composer-service`, `libsdmcore.so` (`sdm::HWPeripheralDRM::{GetHWPanelMaxBrightness,SetPanelBrightness,GetPanelBrightness}`) formats `/sys/class/backlight/panel%d-backlight/` and accesses `max_brightness` and `brightness` through `sdm::Sys::open_` (`_ZN3sdm3Sys5open_E` in `libsdmutils.so`).
3. On kernel 6.18 with `pwm-backlight` (`OF_FULLNAME=/backlight`), sysfs exposes `/sys/class/backlight/backlight/{brightness,max_brightness,actual_brightness,bl_power}` (owned by `system:system` with `0664` mode and `max_brightness = 4095`), whereas `/sys/class/backlight/panel0-backlight/` does not exist (`ENOENT`), causing `HWPeripheralDRM::SetPanelBrightness` to fail and `GetHWPanelMaxBrightness` to fall back to `255.0f`.

## Userspace Compatibility Fix

- [sq2-sdm-backlight-shim.c](../probe/graphics/sq2-sdm-backlight-shim.c):
  - Exports `sq2_rewrite_panel_backlight_path()` to rewrite `/sys/class/backlight/panel<N>-backlight/...` to `/sys/class/backlight/backlight/...`.
  - Constructor `sq2_install_sdm_backlight_hooks()` installs `ENOENT`-only fallback wrappers on `libsdmutils.so` function pointers `_ZN3sdm3Sys5open_E`, `_ZN3sdm3Sys7access_E`, and `_ZN3sdm3Sys18inotify_add_watch_E`. Primary `panel<N>-backlight` paths are preserved unmodified whenever present, and non-`ENOENT` errors are never masked.
- [build_sdm_backlight_shim.py](../scripts/build_sdm_backlight_shim.py):
  - Builds stripped ELF64 AArch64 `libsq2_sdm_backlight.so` (linking only `liblog.so`, `libdl.so`, `libc.so`), verifies ELF metadata/exports, and adds `DT_NEEDED libsq2_sdm_backlight.so` to `lib64/sq2-qti-display/libsdmcore.so` via `patchelf --page-size 16384`.
- [full_image.py](../scripts/full_image.py):
  - Wires `build_sdm_backlight_shim.build_payload()` into `hybrid_runtime()` and updates `INSTALL_FILES`, `INSTALLED_SHA256SUMS`, and `SHA256SUMS`.

## Verification

- Host & private-overlay unit tests passed:
  - `tests/test_sdm_backlight_shim.py` (`SdmBacklightShimTest`)
  - `tests/test_full_image_hybrid.py` (`HybridGraphicsTest`)
  - `tests/test_full_image_mesa.py` (kernel & Mesa helper tests)
- Live hardware verification on target device was **not** completed in this session because the target became unreachable over SSH (`No route to host`, likely idle sleep/power-off) before deploying the updated `libsdmcore.so` + `libsq2_sdm_backlight.so`.
