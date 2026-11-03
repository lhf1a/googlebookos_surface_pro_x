# LTE development checkpoint — 2026-11-02

## Superseded by the 2026-11-03 resume

This file describes the immutable 666e56f checkpoint, not the current device.
The operator's original FSG has now supplied working RF state without clearing FTM;
LTE registration, a real QMAPv4/RMNET bearer, Android metered NetworkAgent integration,
Wi-Fi/default-network fallback and Chrome HTTPS traffic have passed. The first full
boot restored LTE automatically and preserved audio/EL2/screen-only checks. The final
candidate has also passed a normal reboot after promotion to the default entry.
See [LTE-DATA.md](LTE-DATA.md) and the latest [HANDOFF.md](HANDOFF.md) for final boot
deployment status and remaining radio HAL/voice/SMS limitations. The historical
failure statements below must not be used as current diagnoses.

## What is and is not working

The experimental SQ2 environment has a bound IPA driver and `rmnet_ipa0`, its own
generated NV, active production hardware MCFG, and automatically selected ROW
software MCFG. The modem still reports factory-test mode 2, no registration,
PS detached and WDS disconnected. **LTE is not usable yet.**

The latest helper recovered service 21's exact IDL tables from the original MPSS
and matched its request frame with Qualcomm's official IDL codec. On hardware,
Get Supported Messages/Fields succeeded; the fixed nonexistent-path request
returned QMI error 1 (MalformedMessage), not a filesystem errno. No RF file was
read. Do not interpret that rejection as missing calibration or try opcode 0x20.

The last complete health observation was before this MFS experiment. The save-time
SSH recheck timed out; no post-experiment health success or board reboot is claimed.
Normal Qualcomm/Mesa entries were not replaced and no reboot was requested.

## Saved material

- Public branch: `codex/lte-support`; no merge into `main` is implied.
- Development source, reference analyses, scripts, tests and HANDOFF/BUILD/LTE
  notes are committed with their actual failure limits.
- `artifacts/lte/20261102/`: exact tested IPA module, experimental DT and EFS
  helper, with SHA-256 manifest. These are manual lab artifacts only.
- The full private checkpoint retains the four transaction roles and their
  supplemental records, original firmware/driver packages, calibration inputs,
  candidate and baseline boot binaries, failed attempts, native analysis and
  observed output. It must be encrypted before any upload to the public origin.
- SSH private keys and the checkpoint decryption key are never included in the
  public repository or the uploaded archive. Keep the local recovery key safely.

## Resume point

First re-establish and verify the existing device boot/environment without
resetting adopted remoteprocs. Trace the original `fs_qmi_service` validation for
message 0x21 using the recovered schema and paged-code references, then correct
only an evidenced mismatch. Do not resend unchanged DIAG/online attempts, clear
FTM, swap more entire NV images or promote experimental payloads.

Completion still requires actual registration/PS attach, bearer, cellular IP and
routes, proven cellular traffic, and Android integration—not merely a netdev.
