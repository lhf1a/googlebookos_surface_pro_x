# Surface Auto-Brightness and Auto-Rotation

Shared component fingerprints are recorded in `configs/sensors-baseline.json`. The closed-source SLPI firmware, model configs, and default optical calibration are shared across SQ1/SQ2 devices without per-unit extraction.

## Default Build

`build.py full-image` requires sensor inputs (`PRIVATE_ROOT/sensors` by default, or `--sensor-inputs`). See [STANDARD-BUILD](STANDARD-BUILD.md) for the overall flow.

```sh
python3 scripts/build.py full-image --graphics hybrid \
  --private-root /path/to/private-inputs --sensor-inputs /path/to/sensors \
  --camera-inputs /path/to/camera --output out/new-full-image --esp-only
```

In `source` kernel mode, TCS3430, SLPI PAS, and FastRPC modules are rebuilt against the active kernel build; `prebuilt` mode verifies ARM64 architecture and kernel ABI. SLPI firmware is installed into the ESP firmware tree, and `qebspil` with PAS12 is installed paired with the corresponding DT.

Prepare source artifacts and shared OEM assets before assembling the sensor bundle:

```sh
bash scripts/build-spx-sensors.sh KERNEL_SOURCE MATCHING_BUILD NEW_ALS_OUTPUT
python3 scripts/build-spx-slpi.py KERNEL_SOURCE MATCHING_BUILD NEW_SLPI_OUTPUT
python3 scripts/build-spx-sensor-hal.py AOSP_INTERFACES TOOLCHAINS MATCHING_LIBRARIES NEW_HAL_OUTPUT
python3 scripts/build-spx-sensor-overlay.py TOOLCHAINS NEW_OVERLAY_OUTPUT
python3 scripts/extract-spx-light-calibration.py SurfaceLightSensor.sys GOLDEN_OUTPUT
python3 scripts/prepare-spx-slpi-efi.py MATCHING_EFI_SOURCE NEW_EFI_SOURCE
```

The HAL uses the AOSP sensors 2.1 SubHal interface and platform-matching HIDL/libc++ ABI; the RPC file service uses a static ARM64 build of `linux-msm/hexagonrpc` (`da7a3742ab8d5273a281b70ee11f16fb2cb6a690`). Generate the private input manifest with:

```sh
python3 scripts/build-spx-sensor-bundle.py \
  --modules MODULE_DIR --hal HAL_DIR/sensors.spx.so \
  --overlay OVERLAY_DIR/spx-sensors-overlay.apk --rpc HEXAGONRPCD \
  --config OEM_MODEL_CONFIG_DIR --calibration GOLDEN_OUTPUT \
  --firmware qcslpi8180.mbn --efi qebspilaa64.efi \
  --output private/build-inputs/sensors
```

Import only model JSON/`config_list`, model firmware, and default optical calibration—never another device's registry, persist, or identity data.

## Mechanism and Fixes

- **ALS & SLPI**: TCS3430 on I2C `0x888000`/`0x39` outputs XYZ/IR per integration frame, converted to lux using the model default calibration. SLPI boots via EFI PAS12, preserving the `0x99000000`/20MiB SCSS memory boundary without touching ADSP.
- **HAL & QRTR**: The accelerometer HAL dynamically discovers the SSC service and accel SUID without full `qsh` enumeration, and calls `getsockname` before `bind` on QRTR.
- **Rotation Overlay**: Enables `config_supportAutoRotation` and sets `config.override_forced_orient=true` while preserving the user's rotation lock.
- **SLPI SSC Stream-Stop Handshake (`probe/sensors/spx-ssc.{h,cpp}`, `spx-sensors-subhal.cpp`)**:
  - `SscSensor::disable(250)` sends `kSscMsgClientDisableReq (10)`, waits for the matching QMI response (`ctl_flag=2, msg_id=0x20, txn`), and calls `drain(15)` before closing.
  - On `disable()` success, the open QRTR socket and resolved SUID are reused across sensor toggles, releasing only on explicit close, `DISABLE_REQ` failure, or `QRTR_TYPE_BYE` / `QRTR_TYPE_DEL_SERVER`.
  - `SscSensor::rate()` uses `kSscDeliveryNoWakeup (1)` for continuous streams; `SurfaceSensors::work()` unlocks `mutex` before blocking `disable()`/`close()` and checks `slpiAlive()` (`/sys/class/remoteproc/<slpi>/state` is `attached` or `running`) before `sendto()`.

## Verification

```sh
python3 -m unittest discover -s tests -p 'test_full_image*.py'
python3 -m unittest discover -s tests -p 'test_spx_als.py'
python3 -m unittest discover -s tests -p 'test_spx_slpi.py'
g++ -std=c++17 -fsanitize=address,undefined tests/spx_ssc_test.cpp -o /tmp/spx-ssc-test
/tmp/spx-ssc-test
```
