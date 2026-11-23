# Generic Delivery Rules (PORT Contract)

Applies to build, boot, and image delivery. See root `AGENTS.md` for authority and priority; see [BUILD](BUILD.md) for build entrypoints.

## PORT-01: One Image, Any SQ1/SQ2

A single standard image should run on any SQ1 or SQ2 without per-machine rebuilds, code edits, or device-specific boot bundle swaps. Model differences are handled via SMBIOS/SoC, hardware capabilities, and runtime resource discovery.

## PORT-02: No Single-Device Gating

Serial numbers, NVMe model/serial, MAC, IP/SSH addresses, SSH host fingerprints, original disk UUID/ESP markers, Android user IDs, or historically captured SHAs must never gate generic builds or normal boot.

## PORT-03: Adapt to Runtime Hardware Facts

Never hardcode one-boot phandles, `eventN`, `cardN`, node numbers, or debug host paths. Hardware class/revision quirks must have a hardware basis rather than generalizing one unit's measurements to all devices.

## PORT-04: Separate Image and Local Device State

Generic images must not copy a debug machine's password/shadow, SSH private keys/host identity, Wi-Fi credentials, IMEI/NV/FSG calibration, userdata, or touch calibration onto other machines. Device-unique data comes from the target's own storage or local first-time setup; lab private bundles must be labeled separately.

## PORT-05: Preserve Safety Guards

Module/firmware ABI, artifact integrity, real hardware compatibility, SMMU/resource boundaries, `screen_only`/`nohibernate`, and non-re-bootable firmware protections remain enforced. Disk writes must dynamically identify the target disk/ESP/root source to prevent mis-writes.

## PORT-06: Cross-Device Acceptance

A single artifact must cover SQ1/SQ2, default/fallback entries, and regressions across varying identities, capacities, and enumerated resources; upgrades over existing data must preserve credentials and user settings. Untested revisions or features are labeled honestly in documentation.

## Verification & Environment

- Builds and boot enforce real failure propagation and ABI/format/path/disk-uniqueness constraints; evidence, raw logs, and identity-bearing artifacts stay in ignored `out/` and `private/`.
- Build host is Ubuntu 24.04 WSL with the project mounted at `/mnt/spx-workspace/chromebookOS` from `F:\WSL\SPX-Workspace\chromebookOS-workspace.vhdx`; only launchers reside on Windows.
- Source builds must run on a Linux filesystem to avoid small-file overhead over NTFS/9p; private inputs root defaults to `private/build-inputs/` and is relocatable via `--private-root`.
