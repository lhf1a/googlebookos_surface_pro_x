# SQ1/SQ2 LTE: Product Path

This document supersedes older LTE instructions covering the Radio04 donor, single-unit serial gating, manual FSG porting, Java NetworkAgent, and experimental observers.

## Runtime Chain

1. `scripts/build.py` enables LTE by default, sharing the source and runtime bundle; the non-LTE Mesa fallback remains unchanged.
2. Before entering EL2, the default EFI router reads the local SMBIOS UUID. A valid `EFI/SPX/LTE/<uuid>.bin` cache is reused directly; if missing or corrupted, it locates and reads the local FSG via native SCM `GET_INFO` and a CRC-checked primary GPT, saving the private cache only on the boot medium without writing physical baseband NV. The format capacity limit is 2 MiB.
3. `lte-unit-seed` verifies the local cache source, length, and CRC, then atomically updates `/run/rmtfs/modem_fsg` in RAM while preserving original MPSS, FS1/FS2, and boot order.
4. IPA4.1/DPM/WDA/WDS/RMNET provide the IPv4 bearer; Radio AIDL is the sole controller, and Android manages SIM, network, APN, and mobile data. The AOSP APN table and pinned sources reside in `probe/lte/overlay/`, preserving existing user APN edits.

## Build Inputs and Limitations

- The UUID identifies the dynamic cache source and must not be distributed with images; CRC only detects corruption.
- Proprietary firmware, driver modules, and Binder link libraries must be supplied by the operator; missing required inputs fail the build.
- `ipa_sq2_lab.ko` is the current active driver name.
- LTE IPv4 data is supported; voice, SMS, IMS, eSIM, 5G, and IPv6 are not supported.
