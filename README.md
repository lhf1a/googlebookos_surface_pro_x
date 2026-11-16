# Googlebook OS on Surface Pro X

The only default full-image build is **hybrid**, shared by **SQ1/SQ2**, using Linux **6.18.60-sq2-android1**: Mesa GLES/Turnip with QTI gralloc/mapper5/HWC and native OMX.

[简体中文](README.zh-CN.md) · [Standard build](docs/STANDARD-BUILD.md) · [Windows dependencies](docs/DEPENDENCIES.md) · [Sleep integration and image hashes](docs/HYBRID-SLEEP-20261116.md)

Camera, touch, automatic brightness/rotation and the Venus s2idle fix are included. The generic image removes inherited accounts/Wi-Fi/SSH credentials, creates fresh userdata/metadata and preserves first-run setup.

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

Prepare shared OEM inputs on the original SQ1 or SQ2 Windows installation with `scripts/Prepare-HybridDependencies.ps1`; copy its tar package and checksum to the Linux build host. Public downloads and Git revisions are pinned. This collection package does **not yet** provide a complete clean-room Android/QTI runtime/template build; see the dependency guide for remaining prepared inputs.

Completed components can be reused with `--reuse-components`. `rebuild-hybrid-venus.py` rebuilds only Venus against the exact matching KOUT before repacking. Build, rollback and full-image readback evidence are recorded in the linked guide; the new disk image has not been boot-tested on SQ1/SQ2.

`verify`/`package` preserve the historical pinned v21 bundle, and `compile` builds kernel components. They are not alternative default full-image lines. Historical build notes are in [BUILD.md](docs/BUILD.md).

Private firmware/libraries, credentials, disk images and raw captures stay outside Git. The reviewed external kernel source/DT input is documented under [source-dependencies](source-dependencies/kernel-6.18/README.md). Component licenses remain applicable. Long-term suspend/power and SQ1 hardware validation are not established by host build success.
