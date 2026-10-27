# Working agreement

Read docs/HANDOFF.md and docs/BUILD.md before changing the port.

- Shared storage was repaired by loading FUSE before Android init. Both emulated volumes mount and the user confirmed Files and Chrome downloads work. Preserve this fix; never format userdata to repair a mount failure.
- True suspend repair remains paused. The user requested screen-off only: preserve the kernel suspend.screen_only guard and nohibernate boot option. Screen-off/on checks are authorized once the guard is verified; never disable it or resume actual sleep tests without a new request.
- Audio work was resumed by the user on 2026-10-27: investigate stop-pop, low/thin/compressed sound and stereo image. Keep gain changes bounded, preserve matching DAC/COMP/BOOST SoundWire ports, and distinguish listening results from register/PCM checks.
- Preserve mapper import/multiplane graphics and touch state fixes. Default contact detection/stability plus AspectMax=2.0 were validated; do not reintroduce high detection thresholds.
- Keep the normal audio and explicit rescue entries plus shutdown. The user removed the old no-audio second entry. Normal boot must not run diagnostic Wi-Fi association/DHCP, rollback or continuous logging; Android owns saved Wi-Fi state. The user subsequently requested automatic public-key SSH: start it asynchronously after Android boot via ssh-framework-boot (sq2.ssh=0 opts out), without enabling diagnostic networking. Rescue or sq2.debug=1 retains full diagnostics. Audit concatenated initramfs in last-entry-wins order; back up, validate gzip/hash, sync and replace atomically.
- Identify the physical disk, ESP marker, UUIDs and root source before disk operations. Old USB scripts deliberately reject NVMe; do not weaken guards to reuse them blindly. Never touch the workstation system disk.
- Do not routinely unload active QCA Bluetooth transport modules: this previously hung the board. Use the documented HAL/bridge path.
- Initialize Bluetooth transport only after Android boot completion; preserve BluetoothManager's persisted bluetooth_on choice. Do not force enable on boot or add a second state file that can override airplane/user policy.
- Distinguish framework restarts (system_server/SurfaceFlinger PIDs) from board reboots (boot ID). A user long-press reboot is not an autonomous crash.
- PM debug-stage completion does not prove actual suspend works. A framework wake lock does not reliably prevent sleep on this port.
- Keep private/, out/, downloads/, upstream/, work/, keys, credentials and raw logs out of Git. Use the operator's own SSH identity and host verification.
- Report observations and limits honestly; do not label untested patches as working features.
- Root/permissive/debug builds are authorized in this lab. This does not authorize erasing an unidentified disk or publishing secrets.
