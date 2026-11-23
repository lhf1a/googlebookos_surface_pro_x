# Surface Advanced Settings and On-Device Management

> Related docs: [README (EN)](../README.md) · [README (中文)](../README.zh-CN.md) · [Hall + IR Face Unlock Plan](IR-HALL-FACE-UNLOCK-PLAN.md) · [System Update (OTA) & Flashing Handoff](OTA-HANDOFF.md) · [Touch Calibration](TOUCH-CALIBRATION.md) · [Type-C & USB Debugging](TYPEC.md)

The Surface Pro X port includes **Advanced Settings (`SurfaceAdvancedControl.apk`, `org.surface.advancedcontrol`)**, providing unified graphical management for performance/graphics tuning, touch calibration, wireless ADB / root SSH debugging, on-device OTA updates, and USB LiveCD installation to the internal NVMe drive.

---

## 1. Architecture and Security Design

- **UI & Localization**: Built with Material 3 / Material You styling, supporting light/dark themes, dynamic color, and automatic Chinese/English switching (defaulting to English on non-Chinese locales).
- **Root Bridge**: The app runs with normal system app privileges and requires no manual KernelSU grant. Privileged operations are forwarded to initramfs root daemon scripts:
  - `/bin/spx-advanced-control-boot`: Handles Vulkan policies, CPU governors, touchscreen disable/ghost-touch/calibration, Hall sensor and OV7251 IR face unlock toggles, network ADB, and Dropbear SSH.
  - `/bin/spx-ota-boot`, `spx-ota-stage`, `/bin/spx-internal-install`, `/bin/spx-ota-tool`: Handle OTA verification/staged updates and full-disk installation from USB LiveCD to the internal drive.
- **Structured Request Boundary**: The app and bridge scripts communicate via nonce-tagged request files (`/run/spx-advanced-control-request`, `spx-ota-boot-request`), strictly validating package names, governor names, public key formats, block device paths, and file size limits without concatenating or `eval`-ing untrusted command strings.
- **Persistence & Rescue Protection**: Settings are stored under `/data/vendor/spx-advanced-control` and `/data/vendor/sq2-touch-calibration` and automatically restored on normal boot; in Rescue Mode `sq2.rescue=1` or with `sq2.cpufreq=0`, debugging, touch-disable, and governor restoration are skipped.

---

## 2. Home Screen Module Overview

| Module | Chinese Name | Core Purpose |
| :--- | :--- | :--- |
| **Vulkan Configuration** | Vulkan 配置 | Global and per-app switching between Mesa Turnip / Qualcomm QTI Vulkan drivers |
| **Performance Mode** | 性能模式 | Real-time switching and persistence of all-core cpufreq governors |
| **Touch** | 触控 | Disable touchscreen, toggle ghost-touch patch, view status, and import/export/recalibrate |
| **Debugging** | 调试 | Toggle network ADB (`5555`), initramfs `root` SSH (`22`), manage passwords and public keys |
| **Lid & IR Face Unlock** | 霍尔与红外解锁 | Toggle Hall lid wake, monitor keyboard posture, control OV7251 IR face unlock (Class 2 / WEAK) and self-test |
| **System update** | 系统更新 | Select local `.tar.gz` / `.img` / `.img.gz` for data-preserving or clean-wipe system upgrades |
| **Install to Internal Disk** | 写入内置硬盘 | Install the OS from a running USB LiveCD to the internal NVMe SSD |

---

## 3. Module Details

### 3.1 Vulkan Configuration

- **Global Default Driver**: Switch between **Auto Policy**, **MESA (Turnip)**, and **Qcom (QTI)**. When set to **Qcom (QTI)**, system UIDs (`< 10000`) and `com.android.systemui` remain forced onto Mesa Turnip to prevent launcher and system UI anomalies.
- **Per-App Overrides & Pinning**: Assign Default Policy / Mesa Turnip / QTI Vulkan per app; manually overridden apps are pinned to the top.
- **Applying Changes**: Provides **Save** (next launch) and **Save & Force Stop** (immediately stops the target app to reload the driver). Regular apps use the QCOM `VM_BIND` cached submission path by default.

### 3.2 Performance Mode

- **Dynamic Discovery**: Enumerates `/sys/devices/system/cpu/cpufreq/policy*/scaling_available_governors` to list governors commonly supported across all CPU clusters (e.g., `schedutil`, `performance`, `ondemand`, `conservative`, `powersave`, `userspace`).
- **Immediate Effect & Persistence**: Applies immediately to all clusters and persists to `/data/vendor/spx-advanced-control/cpu_governor`, restoring automatically on boot. Tap **Restore Default** to revert.

### 3.3 Touch

- **Disable Touchscreen**: After confirmation, stops `iptsd` and persists the disabled flag; can be re-enabled in Advanced Settings (automatically ignored in Rescue Mode).
- **Ghost-Touch Patch**: Toggles the noise-filtering patch via `.noise.disabled` while preserving collected noise calibration samples.
- **Live Calibration Summary**: Displays active `SPXNOISE1` / `SPXNOISE2` / `SPXNOISE3` / `SPXTAP1` status, `ContactSizeMax`, `iptsd` running state, and touch hardware ID.
- **Calibration & Backup/Restore**:
  - **Recalibrate**: Launches `org.surface.touchcalibration.unified`.
  - **Export Calibration**: Exports current touch and ghost-touch configuration into a `.spkcal` file.
  - **Import Calibration**: Reads `.spkcal`, `.profile`, and `.noise` files (max `132096` bytes per file), validates, and reloads `iptsd`.
  - **Clear Calibration**: Clears saved calibration and noise samples.

### 3.4 Debugging

#### Live Debug Addresses
Scans network interfaces, filtering out `lo`, `rmnet*`, `tun*`, etc.:
- **LAN IP**: LAN IPv4 address on a physical USB NIC or Wi-Fi.
- **VM IP**: Linux virtualization bridge (`avf_tap*`, `vmnic*`, `crosvm*`) addresses.

#### Network ADB (Port `5555`) & Lower USB-C Mode
- **Network ADB**: One-tap toggle for TCP `5555` network ADB, persisted to `/data/vendor/spx-advanced-control/adb_tcp_enabled` with automatic firewall updates.
- With `ro.adb.secure=1` enabled, first connection requires RSA authorization on the tablet screen:
  ```sh
  adb connect <LAN_IP>:5555
  ```
- **Lower USB-C Mode** (**Auto** / **ADB Only** / **Host Only**): Invokes `/bin/spx-lower-usb` and `/bin/spx-lower-usb-watch` to switch the lower port automatically or manually between wired ADB and DP/USB Host, persisted at `/data/vendor/surface-usb/lower-mode` (locked to Host when booted from lower-port USB storage).
- **Lower-Port USB-C ADB Coexistence**: Lower-port USB-C forwards loopback ADB via `/vendor/bin/adb-usb-bridge`. When Network ADB is turned off, the bridge script narrows `service.adb.listen_addrs` to `tcp:localhost:5555` so lower-port USB ADB continues working.

#### SSH Service (initramfs `root`, Port `22`)
- Manages the BusyBox **Dropbear** SSH service in the initramfs root namespace, remaining accessible even if Android UI freezes. Toggle state and firewall rules persist across reboots.
- **Password Management**: Default images lock `root` in `/etc/shadow` (`!`); **Change Password** stores a SHA-512 hash, and **Clear Password** re-locks password login to public keys only.
- **Public Key Management & `ssh-copy-id`**:
  - **Import Public Key / Certificate**: Pick a file or paste OpenSSH public keys (`ssh-ed25519`, `ssh-rsa`, `ecdsa-sha2-nistp256/384/521`, `sk-ssh-ed25519@openssh.com`, `sk-ecdsa-sha2-nistp256@openssh.com`, max `65536` bytes) or X.509/PEM certificates.
  - `/root/.ssh` symlinks to `/data/vendor/spx-advanced-control/ssh/root_ssh` and Dropbear's Ed25519 host key is stored at `/data/vendor/spx-advanced-control/ssh/dropbear_ed25519_host_key`, so `ssh-copy-id root@<LAN_IP>` persists across reboots.
  - **Clear Keys**: Removes all authorized `authorized_keys`.

> For pre-Android boot failures, select Rescue Mode in GRUB; see [BUILD — Section 7](BUILD.md#7-boundaries).

### 3.5 Lid & IR Face Unlock

See [Hall Sensor + OV7251 IR Face Unlock Implementation Plan](IR-HALL-FACE-UNLOCK-PLAN.md).

- **Hall Switch (`SW_LID`)**:
  - Dynamically discovers `/sys/devices/platform/gpio-keys-lid` (or the `gpio-keys` node whose `switches` contains `0`) and reads/writes `disabled_switches`, persisted to `/data/vendor/spx-advanced-control/hall_lid_enabled`.
  - Enforces disabling `SW_LID` when `spx.hall_lid=0` or `sq2.rescue=1` is set.
  - **Live State & Fold-Back Protection**: Shows `OPEN` / `CLOSED` / `FOLDED_IGNORED` / `DISABLED` and SSAM keyboard posture (`laptop` / `closed` / `folded-back` / `folded-canvas` / `book` / `disconnected`), suppressing false lid triggers when folded behind the tablet.
- **OV7251 850nm IR Face Unlock (Class 2 / `WEAK`)**:
  - When `ov7251` is detected at boot and neither `spx.face=0` nor `sq2.camera=0` is set, `/bin/spx-face-stage` registers `android.hardware.biometrics.face.IFace/default`.
  - Supports toggling `persist.spx.face.enabled` (when disabled, the HAL rejects auth/enrollment before opening the camera so the IR floodlight never illuminates) and clockwise sensor rotation `persist.spx.face.rotation` (`0°` / `90°` / `180°` / `270°`, default `90°`).
  - Provides **Open System Face Unlock Settings** and a one-tap diagnostic self-test via `spx-face-cli` (`--self-test`, `--status`, `--benchmark-ir 5`).

### 3.6 System update

- Wired into Android's **System update** entry (`android.settings.SYSTEM_UPDATE_SETTINGS`) and the Advanced Settings home screen.
- Supports `.tar.gz` update archives and `.img.gz` / `.img` full images, with **Keep user data (Default)** (preserving `/data` and Windows dual-boot `grub.cfg`) or **Wipe data and clean install** (reformatting `userdata` and `metadata`).
- ESP-only updates write online and prompt to reboot; updates touching `super` or full images stage via `/bin/spx-ota-stage` and execute offline with SHA-256 verification in early initramfs. See [OTA-HANDOFF](OTA-HANDOFF.md).

### 3.7 Install to Internal Disk (LiveCD Mode)

When booted from external USB, install directly to the internal NVMe drive:

1. **Prerequisites** (checked by `/bin/spx-internal-install`): Booted from USB (`BOOT_TRANSPORT=usb`), standard 5-partition source layout with intact ESP, and an internal non-USB block device (e.g., `/dev/nvme0n1`) meeting minimum capacity.
2. **Confirmation & Write**: Connect AC power, select the target internal disk, and confirm irreversible erasure (all existing partitions/data on the internal disk will be erased, and USB `userdata` is not migrated). Installation runs: unmount residuals -> rebuild 5-partition GPT -> write standalone ESP -> copy `super` with SHA-256 readback -> initialize `metadata`/`misc`/`SQ2PROBE`/`userdata`.
3. **Completion & First Boot**: Tap **Power off now**, unplug the USB drive after complete power-off, and power on to boot from internal NVMe (auto-expanding `userdata` on first boot). To keep Windows same-disk dual-boot, use `tools/Install-GooglebookDualBoot.cmd` / `.ps1` on Windows instead.
