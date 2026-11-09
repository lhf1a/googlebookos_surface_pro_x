# Googlebook OS on Surface Pro X

The sole supported baseline is **v21-release, Linux 6.18.60-sq2-android1**: mapper5, original Qualcomm gralloc, and source QTI HWC/SDM.

[简体中文](README.zh-CN.md) · [Build guide](docs/BUILD.md) · [Current status](docs/HANDOFF.md)

The operator accepted v21 speaker audio and Bilibili page transitions. The release cleanup removes bundled diagnostic tools, persistent capture, bridge buffer logging and Venus diagnostic switches. It retains the SoundWire and DPU EBUSY fixes and the original OMX codec.

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/v21-release-FRESH
python3 scripts/build.py compile --help
```

The default private input is `private/build-inputs/kernel-6.18/EFI/SQ2Nat`. `configs/kernel-baseline.json` pins the eight EFI files; initramfs SHA256 is `e826df919c358a61ff023f6021ba9dae43d166b1d781a3b107ed8320286456a0`. `package` verifies and copies them exactly. `compile` produces a separate unvalidated candidate, not a replacement accepted release.

Historical Mesa/debug packages are retired as supported releases. The existing on-device fallback remains for recovery. Local firmware, private libraries, credentials and raw captures stay outside Git; this device-specific bundle is not an anonymous public image.

The internal panel is SDR-only and Main10 remains filtered. Full-stack migration, long-term suspend/stability and SQ1 validation are still incomplete; functional acceptance does not establish those claims.
