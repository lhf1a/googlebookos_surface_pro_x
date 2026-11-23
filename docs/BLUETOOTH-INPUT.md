# SQ2 Bluetooth HID Input

Bluetooth UHID bus 0005 event nodes require `root:input 0660` permissions and the `input_device` SELinux context before EventHub can open them. Without this, EventHub fails with `EACCES`, the UHID ready timeout expires, and Android disables the HID connection policy.

`probe/test2/bluetooth-input-watch` polls every second and republishes only Bluetooth UHID bus 0005 event nodes with `root:input 0660` and the `input_device` SELinux context. It re-verifies sysfs identity before publication, tracks device identities rather than event numbers, and leaves a node untouched if ownership or context setup fails. Existing Surface cover and IPTSd paths are unchanged.

`bluetooth-framework-boot` starts the watcher before transport publication, including on its existing-bridge early-return path, using a PID/cmdline check to avoid duplicate starts. It does not modify transport state, pairing databases, or input events.
