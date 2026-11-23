# Type-C: Dual-Port DP, Native Multi-Display Settings, and Lower-Port ADB

The default Hybrid `full-image` build includes dual-port DP, native multi-display arrangement/mirroring/extension, and lower-port USB ADB. When no saved selection exists, the lower port defaults to ADB; `--save` selections persist across reboots. ADB enforces Android authorization (`ro.adb.secure=1`).

## Usage

Connect the lower port to a host computer and authorize USB debugging on the device:

```powershell
adb devices -l
adb -s SERIAL shell
```

Run in initramfs root SSH to inspect or switch the lower-port mode:

```sh
/bin/spx-lower-usb status
/bin/spx-lower-usb adb --save
/bin/spx-lower-usb host --save
```

`adb` sets the lower port to USB device; `host` restores DP, hub, and storage host functions. Disconnect any lower-port display or peripherals before switching to `adb`. Omitting `--save` applies to the current session only; saved mode is stored at `/data/vendor/surface-usb/lower-mode`.

## Integration Mechanism

- Reuses the lower-port DRD/UDC and FunctionFS module matching the current `Image`.
- Waits for actual UDC registration after asynchronous DWC3 role switching before setting `sys.usb.controller`.
- Stops the QTI USB HAL to prevent gadget ownership conflicts with mainline configfs.
- Wires `ffs.adb` into Android's `sys.usb.configfs=1` init rules and `adbd`.
- Restores the saved mode after `boot_completed` once the gadget function is created, defaulting to lower-port ADB if unset.

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
