# SQ2Hyb Clean GMEM Turnip + Dual-Vulkan Coexistence (2026-11-14)

- Status: `hardware_validated: true` on `192.168.8.124` (`EFI/SQ2Hyb/initramfs.cpio.gz`, `boot_id=4c62129d-c042-4bea-9173-b9199f8c3269`; interactive full benchmark/gameplay soak remains user-side).
- Worktree: `/mnt/spx-workspace/chromebookOS/out/wt-hybrid-20261113` (branch `codex/hybrid-mesa-qti-gralloc-20261113`)
- Output candidate: `/mnt/spx-workspace/chromebookOS/out/sq2hyb-dual-vulkan-20261114/initramfs.cpio.gz`

## 1. Architecture Summary

This change removes the legacy default 3DMark `sysmem` workaround from Mesa 26.2.4 Turnip (now that Linux 6.18 carries `0003 Fix GMEM range on a680`) and adds per-process Dual-Vulkan coexistence to `--graphics hybrid` (`SQ2Hyb`):

1. **Clean Hardware-GMEM Turnip**:
   - `scripts/patch-sq2-turnip-gmem.py` now defaults to `--revert` / `--verify-clean` against upstream `tu_cmd_buffer.cc` SHA256 `d61e9642c7daadd6144fbbe20d9f1fe6d1b3f51db64d2c63da886a3182df4fe8`.
   - `scripts/build-sq2-turnip.sh` builds unpatched Turnip by default and asserts absence of `com.futuremark.dmandroid.application` and `SQ2 A680.1` markers in `vulkan.freedreno.so`. Legacy mode remains opt-in only (`--apply-legacy-3dmark-sysmem` / `SQ2_TURNIP_LEGACY_3DMARK_SYSMEM=1`).
   - `scripts/full_image.py` (`hybrid_runtime()` and `mesa_runtime()`) enforces `resolve_clean_turnip_bytes()` to reject legacy 3DMark-patched Turnip binaries.
2. **Staged `/vendor` Layout on `SQ2Hyb`**:
   - `/vendor/lib64/hw/vulkan.freedreno.so`: `SQ2VkRouter` (`probe/graphics/sq2-hybrid-vulkan-router.c`, built by `scripts/build_hybrid_vulkan_router.py`), loaded by Android `libvulkan.so` via `ro.hardware.vulkan=freedreno`.
   - `/vendor/lib64/hw/vulkan.turnip.so`: clean hardware-GMEM Mesa 26.2.4 Turnip (Vulkan 1.3).
   - `/vendor/lib64/hw/vulkan.qti.so`: Quest 2 v77 `vulkan.qti-policy.so` relocated out of `etc/sq2-qti-system/root/lib64/hw/vulkan.freedreno.so` (so `qti-system-display-stage` no longer overwrites the router), delegating to `/vendor/lib64/vulkan.qti-native.so` + `libquest2-native-vulkan-memory.so` + `libq2v77{glnext,qgl}.so` + `qgl_config.txt`.
   - `/vendor/etc/sq2-vulkan-policy.conf`: default per-app and per-type policy (`configs/sq2-vulkan-policy.conf`).
   - Preserved unchanged on `SQ2Hyb`: Mesa Freedreno GLES (`libEGL_freedreno.so` / `libGLES*`), `gralloc.default.so -> gralloc.sq2_mesa.so` (`gralloc.kona.so` + `mapper.quest2.so`), SDM backlight shim (`libsq2_sdm_backlight.so`), native OMX (`RequiresSwRenderer=0`), and global Vulkan 1.3 permission XML (`version="4206592"`).

## 2. Process Classification & Routing Priority

`SQ2VkRouter` inspects UID, `/proc/self/cmdline` basename/package, and `dl_iterate_phdr` loaded SONAMEs in `hw_module_methods_t::open()` to classify each process into one of 8 types (`sq2_vk_type_name`):

| Type | Detection Signal (First Match) | Default Policy (`sq2-vulkan-policy.conf`) |
| --- | --- | --- |
| `system_ui` | Package `com.android.systemui` | `turnip` |
| `system` | `uid % 100000 < 10000` | `turnip` |
| `browser` | Chrome / WebView / Firefox / Edge / `:sandboxed_process` / `libwebviewchromium` / `libmonochrome` / `libchrome` | `turnip` |
| `emulator_vk13` | `com.winlator*`, `org.yuzu.*`, `org.dolphinemu.*`, `org.ppsspp.*`, etc. or `libdxvk` / `libvkd3d` / `libyuzu` / `libbox64` | `turnip` |
| `benchmark` | `com.futuremark.*`, `com.primatelabs.geekbench*`, `com.antutu.*`, `com.kishonti.gfxbench*`, etc. | `turnip` |
| `unreal` | Loaded `libUE4*` / `libUnreal*` / `libUE5*` (with `app com.kurogame.* = qcom` app rule matching before type) | `turnip` (except `com.kurogame.* -> qcom`) |
| `unity` | Loaded `libunity*` | `turnip` |
| `generic` | All remaining app processes | `turnip` |

Evaluation order in `sq2_vk_evaluate_policy()` (highest to lowest):

1. `SQ2_VULKAN_DRIVER=turnip|qcom` environment variable (non-setuid processes only).
2. Kernel `/proc/cmdline`: `sq2.rescue=1` forces `turnip` across all processes; else `sq2.vulkan.force=turnip|qcom` (last valid wins). Boot token `sq2.vulkan=mesa` / `sq2.vulkan=qcom` is ignored so bootloader tokens do not mask per-app QTI rules.
3. Zygote / USAP preload guard (`zygote`, `zygote64`, `webview_zygote`, `usap32`, `usap64`): forced `turnip` and marked uncacheable (`cacheable = 0`) so forked children always re-evaluate independently under their own PID and cmdline.
4. Per-app Android system property lists (read via `__system_property_read_callback` without 92-byte truncation): `debug.sq2.vulkan.turnip_apps` > `debug.sq2.vulkan.qcom_apps` > `persist.sq2.vulkan.turnip_apps` > `persist.sq2.vulkan.qcom_apps`.
5. Per-type properties: `debug.sq2.vulkan.type.<type>` > `persist.sq2.vulkan.type.<type>`.
6. Global driver property: `debug.sq2.vulkan.driver` > `persist.sq2.vulkan.driver` (`system` type `UID % 100000 < 10000` stays on `turnip` unless `debug.sq2.vulkan.include_system=1`).
7. Policy file `/vendor/etc/sq2-vulkan-policy.conf`: `app <pattern> = ...` > `type <type> = ...` > `default = ...`.
8. Built-in fallback: `turnip`.

## 3. Runtime A/B Testing & Rescue Commands

After deploying to a reachable device, per-app switching takes effect on app restart (no reboot required):

```sh
# Observe per-process Vulkan backend decisions
adb logcat -s SQ2VkRouter

# A/B test Wuthering Waves (default qcom -> temporary turnip -> clear)
adb shell setprop debug.sq2.vulkan.turnip_apps com.kurogame.*
adb shell am force-stop com.kurogame.mingchao
# restore default (qcom):
adb shell setprop debug.sq2.vulkan.turnip_apps ""

# A/B test 3DMark Wild Life (default clean GMEM turnip -> temporary qcom -> clear)
adb shell setprop debug.sq2.vulkan.type.benchmark qcom
adb shell am force-stop com.futuremark.dmandroid.application
# restore clean GMEM turnip:
adb shell setprop debug.sq2.vulkan.type.benchmark ""
```

Kernel cmdline rescue options:
- Append `sq2.rescue=1` or `sq2.vulkan.force=turnip` to kernel cmdline to force Turnip globally regardless of properties or `/vendor/etc/sq2-vulkan-policy.conf`.

## 4. Built Candidate & Checksums

Generated by `scripts/pack_sq2hyb_dual_vulkan.py` into `/mnt/spx-workspace/chromebookOS/out/sq2hyb-dual-vulkan-20261114/`:

| Artifact | Size (bytes) | SHA256 |
| --- | --- | --- |
| Output `initramfs.cpio.gz` | 222,909,292 | `f79f97dacddede0f7f4eb22fe067e54d462589701085743150b960496a77a06f` |
| Base `test-backlight-20261113/.../initramfs.cpio.gz` | 207,407,378 | `29827dc4e5035b921165a0de078a0dcae112e0c50bad0682c38b44a9a6c2fa35` |
| Router `/vendor/lib64/hw/vulkan.freedreno.so` | 20,088 | `7ebd4d5af35504d5e180885c76ac9eb2a3240fd0d85ed350ffec25ee74515ab0` |
| Clean Turnip `/vendor/lib64/hw/vulkan.turnip.so` | 14,780,416 | `bb6e4177bc40119942ab8d8c9fe51c9013faae764ef670c6d057d4d1be73b427` |
| Shared `libdrm.so` (Mesa + QTI display) | 94,904 | `e7ff06bbbac3e492e221ecaa0fa593eb9387db5dc932d99c17ad8645704bb087` |
| Policy `/vendor/etc/sq2-vulkan-policy.conf` | 1,311 | `c9aabda6b30eb6da15886b8562a5fbffbd35ab339f955c28d6aa6544024f8000` |
| QTI HAL `/vendor/lib64/hw/vulkan.qti.so` | 24,984 | `b6704dc13dd7336f6b6a8e1f17980f94e7289e35d57d385c1f36b18a3c11ea84` |
| QTI `vulkan.qti-native.so` | 3,649,617 | `3d67ae4ef6cbb7339f1ddaa27cfed61931f82b5fdcd86767b5e8f765bbc95af6` |
| QTI `libquest2-native-vulkan-memory.so` | 6,632 | `95fdf4fcb84ab93bb2f515cfcf2ce81fae7d81731be2b66ba061ea264aebbf0f` |
| QTI `libq2v77glnext.so` | 2,501,201 | `17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3` |
| QTI `libq2v77qgl.so` | 24,527,809 | `100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652` |
| QTI `qgl_config.txt` | 33 | `1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008` |

Reference rollback commands are recorded in `/mnt/spx-workspace/chromebookOS/out/sq2hyb-dual-vulkan-20261114/ROLLBACK.sh`. Full audit details are in `MANIFEST.json` and `VERIFICATION.txt`.

## 5. Verification Status

- Host build & GMEM check: `SQ2_TURNIP_BUILD_OK`, `com.futuremark.dmandroid.application` and `SQ2 A680.1` absent from clean Turnip and absent from the full decompressed `initramfs.cpio.gz` stream.
- Host router ASan/UBSan + AArch64 ELF verification (`test_hybrid_vulkan_router.py`): passed (all 8 app classes, precedence, `>180` char callback property, `fork()` cache invalidation, `-ELOOP`, `-ENODEV`).
- Simulated boot staging dry-run (`/bin/sh -eu` replaying `apply-security-overlay` + `qti-system-display-stage`): passed with 33 staged libs/symlinks and `0` unresolved `DT_NEEDED` entries; trailing SSH credential gzip member preserved verbatim.
- Live hardware verification (`192.168.8.124`): **passed (`hardware_validated: true`)**:
  - Deployed to `/media/esp/EFI/SQ2Hyb/initramfs.cpio.gz` (cold-disk SHA256 `f79f97dacddede0f7f4eb22fe067e54d462589701085743150b960496a77a06f`; pre-deploy candidate `1e96c1c754fb27175f053be15fe1ff3857cd5f47197f9fd6c957379cbd781dd3` backed up at `/data/local/tmp/initramfs.cpio.gz.before-dual-vulkan-20261114`).
  - Clean reboot from `3be7e77b-7410-456a-b712-326e197a7f90` to `4c62129d-c042-4bea-9173-b9199f8c3269` (`6.18.60-sq2-android1`, `sys.boot_completed=1`, `surfaceflinger` + QTI `allocator` + `composer` running with `libsq2_sdm_backlight.so` mapped; all 9 Vulkan/policy SHA256s matched `MANIFEST.json`).
  - Native `vulkan-readback` via `/system/lib64/libvulkan.so`:
    - Default (`turnip`): `Turnip Adreno (TM) 680`, `Vulkan=1.3.354`, `COMPUTE_OK`, `IMAGE_OK`, `RASTER_OK`, `AHB_SUMMARY 3/3` (`bad_pixels=0`), `SQ2_VULKAN_READBACK_OK` (exit `0`).
    - Property override (`debug.sq2.vulkan.qcom_apps=vulkan-readback`): `Adreno (TM) 680`, `Vulkan=1.1.295`, zero-bridge native QTI maps (`vulkan.qti.so` -> `vulkan.qti-native.so` + `libquest2-native-vulkan-memory.so` + `libquest2-system-ion.so` + `libgsl.so` + HIDL `mapper@4.0`), `COMPUTE_OK`, `IMAGE_OK`, `RASTER_OK`, `AHB_SUMMARY 3/3` + `AHB_REEXPORT_OK`, `MEMORY_TYPES_SUMMARY 6/6`, `SQ2_VULKAN_READBACK_OK` (exit `0`).
  - Zygote-forked Android apps (`logcat -s SQ2VkRouter` + `/proc/<pid>/maps`):
    - `com.futuremark.dmandroid.application` (`uid=10252`): default `type=benchmark backend=turnip reason=conf:type` (`PID 17204`, mapped `/vendor/lib64/hw/vulkan.turnip.so`); override `debug.sq2.vulkan.type.benchmark=qcom` -> `backend=qcom reason=prop:type.benchmark` (`PID 18841`, mapped `vulkan.qti.so`, `vulkan.qti-native.so`, `libq2v77glnext.so`, `libquest2-native-vulkan-memory.so`, `libgsl.so`).
    - `com.kurogame.mingchao` (`uid=10253`): default `backend=qcom reason=conf:app` (`PID 25475`, mapped `vulkan.qti.so`, `vulkan.qti-native.so`, `libq2v77glnext.so`, `libquest2-native-vulkan-memory.so`, `libgsl.so`, `libUE4.so`; note `.KrSdkSplashActivity` initialized Vulkan before `libUE4.so` loaded so it classified `type=generic` and matched `app com.kurogame.* = qcom`); override `debug.sq2.vulkan.turnip_apps=com.kurogame.*` -> `backend=turnip reason=prop:debug.turnip_apps` (`PID 28690`, mapped `vulkan.turnip.so`, `libUE4.so`).
    - `com.android.chrome` (`uid=10164`, `:privileged_process0` `PID 7491`): default `type=browser backend=turnip reason=conf:app` (mapped `vulkan.turnip.so` + Freedreno EGL/GLES).
  - Kernel/crash checks: `0` GPU/Adreno/DRM/IOMMU faults in `dmesg` and `0` graphics/app crashes in `logcat -b crash` (only pre-existing `/system/bin/gscd` abort on missing `/dev/tpm0`).
  - Remaining user-side boundaries: interactive full 3DMark Wild Life benchmark scoring runs (clean-GMEM Turnip vs QTI Direct) and extended in-game Wuthering Waves visual/thermal/power evaluation.
