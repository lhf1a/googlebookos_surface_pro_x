# Working agreement

Read docs/HANDOFF.md and docs/BUILD.md before changing the port.

- Shared storage was repaired by loading FUSE before Android init. Both emulated volumes mount and the user confirmed Files and Chrome downloads work. Preserve this fix; never format userdata to repair a mount failure.
- True suspend repair remains paused. The user requested screen-off only: preserve the kernel suspend.screen_only guard and nohibernate boot option. Screen-off/on checks are authorized once the guard is verified; never disable it or resume actual sleep tests without a new request.
- Speaker pop after playback, missing bass and right-biased stereo are recorded/deferred. Do not tune audio without a new request.
- Preserve mapper import/multiplane graphics and touch state fixes. Default contact detection/stability plus AspectMax=2.0 were validated; do not reintroduce high detection thresholds.
- Keep normal audio, no-audio and rescue entries. Audit concatenated initramfs in last-entry-wins order. Back up locally, validate gzip/hash, sync and replace atomically.
- Identify the physical disk, ESP marker, UUIDs and root source before disk operations. Old USB scripts deliberately reject NVMe; do not weaken guards to reuse them blindly. Never touch the workstation system disk.
- Do not routinely unload active QCA Bluetooth transport modules: this previously hung the board. Use the documented HAL/bridge path.
- Distinguish framework restarts (system_server/SurfaceFlinger PIDs) from board reboots (boot ID). A user long-press reboot is not an autonomous crash.
- PM debug-stage completion does not prove actual suspend works. A framework wake lock does not reliably prevent sleep on this port.
- Keep private/, out/, downloads/, upstream/, work/, keys, credentials and raw logs out of Git. Use the operator's own SSH identity and host verification.
- Report observations and limits honestly; do not label untested patches as working features.
- Root/permissive/debug builds are authorized in this lab. This does not authorize erasing an unidentified disk or publishing secrets.
