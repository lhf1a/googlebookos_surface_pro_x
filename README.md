# Googlebook OS on Surface Pro X

Android-based Surface Pro X port. **The only supported kernel build baseline is Linux 6.18 LTS, currently 6.18.60-sq2-android1.**

[简体中文](README.zh-CN.md) · [Build guide](docs/BUILD.md) · [Current progress](docs/HANDOFF.md) · [Migration evidence](docs/KERNEL-6.18-MIGRATION.md)

## Current baseline — 2026-11-07

The accepted bundle preserves the running SQ2 exactly: Mesa, native 60 Hz, GPU700/RPMh432, EL2, unrestricted suspend and nohibernate, with the matched audio/power/touch/video/LTE fixes. Its hashes are in `configs/kernel-baseline.json`; the source-build configuration is `configs/kernel-6.18-baseline.config`.

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/6.18-release-FRESH
python3 scripts/build.py compile --help
```

`package` verifies and copies the accepted private EFI payload; it is not a source build or a full disk-image generator. `compile` performs a real 6.18 build and exports a separate candidate with its matching modules. The old 6.3.8 build chain and old disk repacking entrypoints are retired, not silently redirected to obsolete payloads. See the build guide for required source/toolchain/external modules and private inputs.

## Evidence and limits

- New 6.18 Android boot and automatic touch initialization passed; the operator confirmed the remaining device functions tested.
- Hardware AVC High+B: six frames plus EOS; every output pixel matched the independent reference.
- LTE registered and Android-validated; bound-interface traffic demonstrated. This is not long-term network certification.
- Scene Root mode works after the explicitly authorized per-device cross-user data link. Scene GPU frequency telemetry still needs application-interface adaptation; the kernel devfreq node is readable.
- Camera/sensors remain previously deferred work. SQ1/new full-image cross-device coverage and long-term stability must not be inferred from one SQ2.

The private bundle contains the operator's original SSH identity and is **not a public anonymous image**. Credentials, calibration, user data, raw logs and local source/toolchain archives stay outside Git. Existing on-device fallback payloads are preserved. See [portable development policy](docs/DEVELOPMENT.md).