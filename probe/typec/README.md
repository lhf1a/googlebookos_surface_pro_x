# Surface Pro X Type-C support

- `spx-typec.c`: SAM USC ordered events → native QMP orientation/mux and DRM HPD;
  freezable workqueue, real errors/ACKs, root-only lower data-role selection.
- `spx-ucsi-transport.c`: isolated SAM-registry HID transport, derived from Linux
  `drivers/hid/surface-hid/surface_hid.c` (GPL-2.0+, authors retained in source).
- `ucsi_surface_hid.c`: Jairo Valdivia's GPL-2.0 Surface UCSI transport,
  [upstream PR](https://github.com/linux-surface/kernel/pull/184), revision
  `aa4c309df17c70241067f024ad5e49e2c22c5af1`; adapted to Linux 6.18 operations
  and the observed SPX UCSI HID collection without changing global HID matching.
- Event protocol references:
  [Surface Pro X SAM USB-C discussion](https://github.com/linux-surface/surface-pro-x/issues/35).
- `settings-tile`: only the Android Settings dashboard entry; all topology,
  resolution and mirror/extend controls remain in the installed system Settings.

No firmware, device credentials or ADB authorizations belong here. Build and
verification scope: [Type-C](../../docs/TYPEC.md).
