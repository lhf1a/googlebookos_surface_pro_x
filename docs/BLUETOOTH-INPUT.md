# SQ2 Bluetooth HID input

2026-10-30: MX Master 3S BLE authentication/encryption succeeded, but EventHub failed to open the newly created event node with EACCES. UHID closed, the two-second HID ready timeout expired and Android disabled the HID connection policy. Pairing was not the failed stage.

`probe/test2/bluetooth-input-watch` republishes only Bluetooth UHID bus 0005 event nodes with root:input 0660 and input_device SELinux context assigned before IN_CREATE. It checks the sysfs identity again before publication, remembers identities rather than event numbers, and leaves a node untouched if ownership/context setup fails. The existing Surface cover and IPTSd paths are unchanged. The one-second poll repaired this observed race; it is not proof against every scheduling delay or kernel/Android version.

`bluetooth-framework-boot` starts the watcher before transport publication, also on its existing-bridge early-return path. Its PID/cmdline check avoids duplicate sequential starts. No transport unload, forced enable, saved-state write, input-event injection or pairing-database change is introduced. Logs are per-node publication results, not continuous raw event capture.

## Evidence and limits

- Before: encrypted BLE connection, EventHub Permission denied, UHID ready timeout and local disconnect.
- Temporary fix: event22 republished, Android EventHub enabled CURSOR/EXTERNAL and InputReader MOUSE with both wheels; user reported connected. Fifteen-second getevent capture had no movement/button events, so full functional input remains unconfirmed.
- Offline BASELINE/MODIFIED/ROLLBACK use the same root filesystem fixture: 0600 root:root untouched / 0660 root:input republished / original 0600 root:root restored, all exit 0 and rollback hash matches. Android chcon is mocked only in this offline fixture.
- Persistent addon installed on the identified BC711 SQ2. Effective last-entry-wins audit of runtime+caps, clean and USB combinations changes exactly the two Bluetooth scripts; credentials stay identical and last. Existing images remain untouched and a menu backup is retained. Default Codec2, clean and USB entries reference the addon; original normal/Mesa fallback entries do not.
- Live permanent hook ran twice without changing bridge PID or creating a second watcher. Boot unchanged during installation. No reboot validation or long-term/other-profile result is claimed.

Private raw logs, boot images, menu copies and rollback evidence remain in ignored `out/bluetooth-mxmaster/`. The four original roles are retained and extended by the persistence audit and deployment records. Follow-up: normal reboot, confirm watcher auto-start and reconnect, then verify real cursor movement, buttons and both scroll axes with the operator.
