# SQ2 USB Storage and USB Audio

See [TYPEC](TYPEC.md) for current Type-C, default entry, and bottom-port ADB policies.

## Feature Scope

- **USB storage**: FAT32 USB drives mount as Android `PUBLIC` volumes exposed to the current user via `ExternalStorageProvider` and SAF, without adoptable storage or disk wiping.
- **USB audio**: USB-C audio peripherals are assigned ALSA `card1` while built-in audio keeps `card0`; standard `AudioTrack`/`AudioRecord` without a preferred device automatically route to `USB_HEADSET`.

## Mechanism

`probe/test2/usb-functions-stage` runs after the RAM vendor overlay is ready and before Android `init`. It runs only when `sq2.usb=experimental` is set and is skipped when absent, `sq2.usb=0`, or `sq2.rescue=1`:

1. Verifies `screen_only`, overlay, original `fstab`/audio policy hashes, and bundled payload hashes.
2. Loads matching `snd_usb_audio` with a `-2` bitmask across all 32 card indices to exclude `card0`.
3. Adds `voldmanaged=usbN:auto` discovery rules for the three USB controllers without matching NVMe/loop or adding `formattable`, `encryptable`, or `adoptable`; `/data`, `/misc`, and early FUSE loading remain unchanged.
4. Installs the USB audio policy include and AIDL v4 `IModule/usb` declaration into the RAM overlay; the HAL remains `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea` with built-in DMIC0/1, `CARD_0_DEV_1`, gains, and SoundWire configs unchanged.

## Build and Test

```sh
python3 scripts/pack-sq2-usb-functions.py \
  out/video-decode/runtime-v1/boot-v1.cpio.gz \
  out/video-decode/vp8-limits/caps-addon.cpio.gz \
  out/NEW/usb-functions-addon.cpio.gz
python3 tests/test_sq2_usb_boot.py -v
bash scripts/build-sq2-usb-audio-app.sh
```

- The packer audits the merged last-entry-wins result, keeps the private SSH member last, and leaves credentialed outputs in ignored directories.
- Mainline `build-source-boot.py` injects the same USB payload and Bluetooth input helpers via `sq2-usb-boot-payload.py`; `build-source-disk.py` adds the USB enable flag to the normal entry and `sq2.usb=0` to rescue. Use `--require-usb-bluetooth` before deployment to verify the actual ESP menu and initramfs inside the image.
- Test APK `org.surface.sq2.usbtest` uses standard public APIs for `probe`, `tone`, `roundtrip`, `auto-tone`, and `auto-roundtrip`, signed with a local test key via `SQ2_USB_AUDIO_KEY`.

## Rollback Notes

- To disable the stage, pass boot parameter `sq2.usb=0`.
- **Do not hot-restart `vold`**: `/system/etc/init/vold.rc` contains `reboot_on_failure reboot,vold-failed`, so running `ctl.restart vold` triggers a system reboot.
