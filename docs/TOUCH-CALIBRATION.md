# SQ1/SQ2 Unified Touch Default Baseline

**Standard full-image entrypoint: `python3 scripts/build.py`**. Defaults to `--touch-repair on`, sharing the algorithm across SQ1/SQ2 Default/Fallback; per-device noise models are not distributed with the image. Use `--touch-repair off` or `--touch-bundle DIR` to override; the default component directory is `private/build-inputs/touch`.

## Preparing Default Touch Components from Source

In a Linux/WSL environment with an ARM64 cross-toolchain, Android SDK, and local signing key:

```sh
python3 scripts/build-unified-touch.py \
  --archive IPTSD_3_1_1_TAR_GZ \
  --dependencies IPTSD_SUBPROJECTS \
  --cross-file ARM64_MESON_CROSS_FILE \
  --output private/build-inputs/touch
python3 scripts/build.py --template TEMPLATE_IMG --gnu-efi GNU_EFI --output NEW_OUTPUT
```

Use a fresh output directory for components to avoid mixing versions. Compilation produces the APK, IPTSd, calibration helper, and profile tools, recording all source and payload hashes in the output manifest. Specify the SDK and signing key via `SQ2_ANDROID_SDK` and `SQ2_CALIBRATION_KEYSTORE`.

## Default Calibration Flow

1. Automatically starts a 45-second idle detection 5 seconds after opening the app. If the screen is intact with no ghost-touch issues, users can tap "Screen OK · Skip", confirm in the secondary prompt (which notes it can be re-enabled anytime in Advanced Settings → Touch) to skip calibration, automatically disable the ghost-touch patch via `.noise.disabled`, and write `.prompted` so calibration will not pop up on subsequent boots.
2. After detecting idle anomalies, generates candidates from collected samples, displays "Fix generated" ("已生成修复方案"), and starts a 5-second countdown to retest upon tapping "Apply fix and retest" ("应用修复并复测").
3. Performs a 20-second single-finger swipe to detect accompanying contacts, generate candidates, apply, and retest to confirm.
4. Collects 15 single-finger taps, then proceeds to coordinate and thumb calibration.

Initial testing temporarily disables prior per-device noise/multi-touch policies in RAM, and retesting isolates the current candidate; canceling, exiting, or timing out restores previous policies, and settings persist only upon user confirmation. `SPXNOISE1/2` handle isolated-point/narrow-row schemes, while `SPXNOISE3` handles wide-area/mixed anomalies based on measured electrode amplitudes without fixed screen mask zones. Format updates require updating the protocol 7 helper, tools, and IPTSd together.
