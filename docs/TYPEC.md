# Type-C: Dual-Port DP, Native Multi-Display Settings, and Lower-Port ADB

The default Hybrid `full-image` build includes dual-port DP, native multi-display arrangement/mirroring/extension, and automatic lower-port USB-C role switching. When no saved selection exists, the lower port defaults to `auto`; `--save` and **Advanced Settings** selections persist across reboots. ADB enforces Android authorization (`ro.adb.secure=1`).

## Usage

Connect the lower port to a host computer and authorize USB debugging on the device:

```powershell
adb devices -l
adb -s SERIAL shell
```

Switch from **Advanced Settings → Debugging → Lower USB-C Mode**, or run in initramfs root SSH:

```sh
/bin/spx-lower-usb status
/bin/spx-lower-usb auto --save
/bin/spx-lower-usb adb --save
/bin/spx-lower-usb host --save
```

- `auto` (default): stays in `device` (ADB standby) when idle or connected to a PC (USB A-to-C / C-to-C) or pure PD charger; automatically unbinds the gadget and switches to `host` when a DP display, DP dock, or USB/Ethernet dock is attached, then returns to `device` after unplug debounce (locked to `host` when booted from lower-port USB storage).
- `adb`: locks the lower port to USB device (wired ADB); `host`: locks DP, hub, and storage host functions. Disconnect any lower-port display or peripherals before manually switching to `adb`. Omitting `--save` applies to the current session only; saved mode is stored at `/data/vendor/surface-usb/lower-mode`.

## Integration Mechanism

- Reuses the lower-port DRD/UDC and FunctionFS module matching the current `Image`.
- With `lower_auto_dp=1`, `spx-typec` emits a `SPX_LOWER_DP=pending` uevent on lower-port DP Alt Mode requests and briefly waits for userspace to switch to `host` before applying QMP PHY + DRM HPD and ACKing SAM (waking immediately on `s2idle` suspend).
- Event-driven `/bin/spx-lower-usb-watch` listens on `NETLINK_KOBJECT_UEVENT` (zero polling), combining `spx-typec` `lower_dp_state`, UCSI `port0-partner`, and lower-port xHCI devices to dispatch `/bin/spx-lower-usb apply-role`.
- Waits for actual UDC registration after asynchronous DWC3 role switching before setting `sys.usb.controller`.
- Stops the QTI USB HAL to prevent gadget ownership conflicts with mainline configfs.
- Wires `ffs.adb` into Android's `sys.usb.configfs=1` init rules and `adbd`.
- Restores the saved mode after `boot_completed` once the gadget function is created, defaulting to `auto` if unset.

## Build and Inputs

`full_image_typec.py` runs by default in `build.py`:

```sh
python3 scripts/full_image_typec.py --services ORIGINAL_SERVICES_JAR --output PRIVATE_ROOT/typec
```

Source builds require only `ORIGINAL_SERVICES_JAR`; explicit prebuilt builds also pass `--modules MATCHED_MODULE_DIR --image EXACT_IMAGE`. Use a fresh output directory to avoid mixing inputs.

Build flow:

1. Prepare `system/framework/services.jar` into `PRIVATE_ROOT/typec/services.jar`, patching the two `InputManager` initial `UNKNOWN SW_LID` notifications via `baksmali`/`smali` and `zipalign`.
2. In source/reuse mode, `build-spx-typec.py` obtains `source`/`KOUT`/`cross` from the kernel build audit, applies the QMP lifecycle patch, and recompiles the five modules; prebuilt mode reads `typec/prebuilt.json`.
3. Compile and install the Settings tile from `probe/typec/settings-tile` using Android SDK (`SQ2_ANDROID_SDK`), Java, `baksmali`, `smali`, `zipalign`, and a local signing key (`SQ2_TYPEC_KEYSTORE`).
4. `patch-spx-composer.py` adapts mode groups and DRM hotplug filtering in the QTI composer for known ELF layouts (equivalent source patch: `patches/spx-hwc-multidisplay.patch`).
5. Derive board DTs with dual DP/PHY graphs and lower-port OTG while preserving internal display, reserved memory, and firmware.
