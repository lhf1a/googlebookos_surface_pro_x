# KernelSU Next / Zygisk / Device Faker

Integration is based on the 6.3.8 kernel source without migrating to GKI or using prebuilt phone kernels.

## Kernel Integration

- Baseline: `v3.2.0-legacy`, commit `9b08e88862000d5c50fb2e43a5b75123cf472e54`, version `33129`.
- `patch-sq2-ksunext-63.py` adds missing policy copy declarations and cross-translation-unit policy layout macros.
- `prepare-sq2-ksunext.py` unpacks the hash-pinned original surface kernel in `/root/.cache/sq2-ksunext` and reuses the memfd, Bluetooth, audio, and screen-only patches.
- `build-sq2-ksunext.sh` produces `out/ksunext/Image-candidate`, adding only `CONFIG_KSU` and `CONFIG_KSU_KPROBES_HOOK` over the active config.
- `smoke-sq2-ksunext.py` requires `lsm=selinux enforcing=0` for QEMU checks.

## Default Build and Manager

The default boot package integrates the pinned KSU kernel, official `ksud`/manager, and async first-boot helper, while the rescue menu uses an independent non-KSU kernel. Pass `--ksu off` to disable integration; spoofing modules are not installed by default.

- Manager package: `com.rifsxd.ksunext`, official Next 3.2.0, installed on `user10` with signature size `0x3e6`.
- Existing Next `init.rc` injection triggers the module lifecycle without modifying the original initramfs.

## Modules and Device Templates

Spoofing uses per-app process isolation to avoid global model, SDK, or system DPI changes:

- **Zygisk Next 1.5.2** and **Device Faker v1.6.0**, reusing `Seyud/device_faker`.
- Config repository pinned at `0a49b74539cb138e5369ea989b7be411bcbba017`, targeting only `com.tencent.mm` and `com.tencent.mobileqq`:
  - WeChat: `transcend/Samsung/samsung_z_fold6.toml`, model `SM-F9560`.
  - QQ: `transcend/Xiaomi/xiaomi_pad_6.toml`, model `23046RP50C`.
