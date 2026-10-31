# SQ1 EL2 preparation — 2026-10-31

Branch: `codex/sq1-el2-preparation-2026103`. Independent worktree; main's
uncommitted work and private device state were not copied or modified.

## Findings

- User reports WSL2 works on Surface Pro X. This supports Windows virtualization
  capability, not a demonstrated EL2 handoff to our Linux loader.
- Earlier read-only SQ2 (.194) evidence found EL1 entry in many saved boot logs,
  KVM/VSOCK disabled and missing Trusty/Secretkeeper dependencies. These are SQ2
  observations, not fresh SQ1 (.248) EL2 measurements.
- [Secure Launch research](https://github.com/TravMurav/Qcom-Secure-Launch)
  describes Qualcomm's signed Windows takeover route. Its detailed analysis uses
  SC7180; applicability to our SC8180X remains to be tested.
- [slbounce](https://github.com/TravMurav/slbounce) provides an EFI implementation
  and isolated `sltest`. Pinned source: c090a8cdafa25e4c99df90f8d6f73f3805d9b397.
  Its DT overlays cover SC7180/SC8280XP/X1E, not SC8180X. Do not substitute another
  chip identity or apply those overlays blindly.

## Offline artifact

`scripts/build-sq1-el2-probe.sh` builds only upstream sltest and records revision,
PE type and SHA256 in ignored `out/sq1-el2-preparation/`. Requires an initialized
pinned upstream checkout, GNU make, ARM64 GNU cross compiler, ELF headers and file.
On Windows-created submodules use Windows Git for metadata checks (a local wrapper
was used); a native Linux checkout avoids drive-letter gitdir incompatibilities.

Build passed: ARM64 PE32+ EFI application,
SHA256 `12e5a3e918c48fb612de9f9ec50f18cadf208090ad91744d4bc5eca1ec0af23b`.
Linker warns about an RWX LOAD segment in the intermediate EFI ELF. This is not a
signature, firmware compatibility, successful takeover, or KVM test.

The pinned test is ACTIVE, not a harmless capability query: it authenticates and
launches TCB code, exits boot services and can halt/reboot/power off. The README's
green-line description differs from test_main.c's final `psci_off()`; do not use
shutdown or a green line alone as proof. Before real tests require explicit
CurrentEL=2 evidence from the executed post-transition path.

## Next gates

1. Read-only SQ1 inventory: firmware version, Secure Boot state, ESP UUID/marker,
   disk identity, active EFI/GRUB and baseline hashes; keep credentials private.
2. Obtain the operator's own Microsoft-signed ARM64 tcblaunch.exe, record version,
   signature/hash and verify the return path expected by this pinned implementation.
   Latest versions may lack it. Do not bundle binaries in Git or fetch random copies.
3. Prepare an isolated/manual EFI test and a dependable original boot route;
   retain screen_only/nohibernate and current661/65Hz working default. No flash,
   normal-boot driver injection, or automatic retry loops.
4. Only after measured EL2 takeover, prepare SC8180X-specific kernel/DT support:
   KVM/VSOCK/VHOST, SMMU/firmware ownership and remoteproc dependencies. Upstream
   calls out remoteproc preboot requirements; audio/charging/GPU must be regressed.
5. Prove a minimal VM first; separately resolve AVF Secretkeeper/attestation before
   claiming Android's Linux Terminal works. Never alter existing lock credentials.

No device was changed/rebooted in this preparation. No working EL2/KVM claim.

## Fresh SQ1 read-only inventory

Firmware reports 4.234.140 (2025-04-30), Toshiba serial/ESP/data/root identity
matches prior SQ1. BOOTAA64.EFI SHA d39ea7f8…; current menu SHA205bde36….
Current cmdline now selects `/boot/ksunext-trial/Image`, with guardY/nohibernate.
This is a newer, externally changed baseline: do not overwrite it using the old
661 deployment menu. No changes to this running trial were made here.
SecureBoot efivar path was absent; state is UNKNOWN, not assumed disabled.
No tcblaunch.exe at the two inspected mounted paths; Windows volumes were not
mounted/scanned, so this is not proof that no local copy exists. Private inventory
is `out/sq1-el2-preparation/sq1-inventory.txt`.
