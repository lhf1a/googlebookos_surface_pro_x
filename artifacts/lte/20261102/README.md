# SQ2 LTE development binaries — 2026-11-02

These are **experimental development artifacts, not a working LTE release**.
Do not replace the normal Qualcomm default or Mesa fallback payloads with them.
The matching source, patches and build-only scripts are on `codex/lte-support`.

- `ipa_sq2_lab.ko`: observed mode-2 binding and `rmnet_ipa0` creation on the
  documented `6.3.8-sq2-android1` lab kernel. No cellular traffic was established.
- `android-ip4.dtb`: the corresponding experimental IPA DT, used only by the
  recorded one-shot lab boot. It is not the normal/default DT.
- `sq2-mfs-readonly-lab`: the exact manually tested QMI EFS helper. Metadata
  requests succeeded; the first file request returned MalformedMessage and the
  helper stopped without trying the remaining RF paths. No write request exists.

See `manifest.json` for byte counts and SHA-256. EFS source is
`probe/test2/sq2-mfs-readonly-lab.c`; its public build script is build-only.
Static binaries built under a different source basename may have a different
ELF symbol/build-ID hash; use the supplied manifest for the recorded binary.

Original vendor firmware, per-device NV, raw logs, private boot packages and the
combined transaction patch are not published here. They belong in the encrypted
complete checkpoint or an explicitly designated private destination. SSH private
keys are excluded from that checkpoint too.
