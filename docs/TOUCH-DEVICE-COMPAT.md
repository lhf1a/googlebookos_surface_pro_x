# Surface Pro X Touch Build Adaptation

## Supported Touch Device IDs

The boot touch helpers (`touch-services` and input node refresh) support both SPI HID device IDs:
- `045E:0C21` (SQ2)
- `045E:0996` (SQ1)

`build-source-boot` explicitly packages all four touch helpers. This device-ID support is independent of the ghost-touch switch: `--ghost-touch-fix off` disables the continuity patch and the 4-cell heatmap patch while keeping touch startup working on both devices.

Regression check:

```sh
python3 tests/test_surface_touch_device_ids.py
```

## SQ2 Heatmap Processing Overhead Optimization

`patch-iptsd-sq2-heatmap.py` avoids copying 46×68 doubles (25,024 bytes) per frame. Instead, it saves four candidate results and writes them back after all reads complete, skipping neighborhood collection and median calculation for candidates below `239/255`.

- Device/dimension guards (`045E:0C21`, `46×68`), thresholds, four-point positions, and neighborhood semantics remain unchanged.
- The continuity patch reports the first stable contact immediately and releases on disappearance/invalidation, retaining the previous stable position for unstable samples.
