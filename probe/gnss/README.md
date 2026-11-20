# Surface Pro X Hardware GNSS (`probe/gnss/`)

Self-contained C11 QRTR / QMI LOC v2 GNSS stack for Surface Pro X (SC8180X MPSS, SQ1/SQ2) requiring **zero extra kernel drivers**.

## Architecture

- [`spx-qmi-loc.h`](spx-qmi-loc.h) / [`spx-qmi-loc.c`](spx-qmi-loc.c): QMI LOC v2 (`QMI_SERVICE_LOC`, service ID `0x10`) wire encoder/decoder matching `freedesktop/libqmi` `data/qmi-service-loc.json`, plus `AF_QIPCRTR` dynamic service discovery.
- [`spx-nmea.h`](spx-nmea.h) / [`spx-nmea.c`](spx-nmea.c): Strict, fuzz-safe NMEA-0183 parser (`$GPGGA`/`$GNGGA`, `$GPRMC`/`$GNRMC`, `$GPGSA`/`$GNGSA`, `$GPGSV`/`$GLGSV`/`$GAGSV`/`$GBGSV`) with XOR checksum validation, UTC epoch conversion, multi-constellation SV tracking, and fix JSON serialization.
- [`spx-gnss-session.h`](spx-gnss-session.h) / [`spx-gnss-session.c`](spx-gnss-session.c): On-demand session state machine guaranteeing `QMI_LOC_START_REQ` is issued only while clients are connected and `QMI_LOC_STOP_REQ` is issued immediately when the last client disconnects.
- [`spx-gnss-daemon.c`](spx-gnss-daemon.c): On-demand Unix stream socket bridge (default `/run/spx-gnss.sock`). Sleeps in `poll(..., -1)` with zero background polling and no open QMI LOC session when idle (s2idle-friendly).

## QMI LOC v2 Wire Protocol over QRTR

### QRTR Discovery (`AF_QIPCRTR`)

Node and port numbers are never hardcoded:

1. Open `socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0)`.
2. Call `getsockname()` to obtain the kernel-assigned `sq_node` **before** calling `bind()` (same requirement as `docs/SENSORS.md` / `probe/sensors/spx-ssc.cpp`).
3. Send `struct qrtr_ctrl_pkt` with `cmd = QRTR_TYPE_NEW_LOOKUP` and `server.service = 0x10` (`QMI_SERVICE_LOC`) to `{ AF_QIPCRTR, local_node, QRTR_PORT_CTRL }`.
4. Read `QRTR_TYPE_NEW_SERVER` from `QRTR_PORT_CTRL` matching `service == 0x10` with a valid non-zero port (`< QRTR_PORT_CTRL`) to discover `(service_node, service_port)` dynamically.
5. Watch `QRTR_TYPE_DEL_SERVER` during active sessions to detect MPSS reset cleanly.

### QMI SDU Envelope & Messages

Over QRTR, QMI messages carry the 7-byte SDU header (`flags[1] | txn_id[2 LE] | msg_id[2 LE] | tlv_len[2 LE]`) with no QMUX framing:

| Message | ID | Direction | TLVs |
| --- | --- | --- | --- |
| `QMI_LOC_SET_NMEA_TYPES_REQ` | `0x003E` | Req -> Resp (+ Ind `0x003E`) | `0x01`: `uint32` NMEA type bitmask (`GGA=0x01 \| RMC=0x02 \| GSV=0x04 \| GSA=0x08 \| VTG=0x10` -> `0x0000001F`) |
| `QMI_LOC_REG_EVENTS_REQ` | `0x0021` | Req -> Resp | `0x01`: `uint64` event mask (`POSITION_REPORT=1<<0`, `NMEA=1<<2`) |
| `QMI_LOC_START_REQ` | `0x0022` | Req -> Resp | `0x01`: `uint8` `session_id` (1); `0x10`: optional `uint32` recurrence (`1`=periodic); `0x12`: `uint32` intermediate report (`1`=enable); `0x13`: `uint32` min interval ms (`1000`) |
| `QMI_LOC_STOP_REQ` | `0x0023` | Req -> Resp | `0x01`: `uint8` `session_id` (1) |
| `QMI_LOC_EVENT_POSITION_REPORT_IND` | `0x0024` | Indication (`0x04`) | `0x01` `uint32` status, `0x02` `uint8` session_id, `0x10`/`0x11` `double` lat/lon, `0x12` `float` hor_unc, `0x18` `float` speed_h, `0x1A`/`0x1B` `float` alt ellipsoid/MSL, `0x1C` `float` vert_unc, `0x20` `float` heading, `0x24` `3x float` PDOP/HDOP/VDOP, `0x25` `uint64` UTC ms, `0x2C` `uint8 + u16[]` sats used |
| `QMI_LOC_EVENT_NMEA_IND` | `0x0026` | Indication (`0x04`) | `0x01`: ASCII NMEA-0183 string |

## Build & Test

Run host ASan/UBSan unit tests and ARM64 cross-compile via `unittest`:

```sh
python3 -m unittest tests/test_spx_gnss.py
```

Or use the builder script directly:

```sh
# Host ASan/UBSan unit test suite
python3 scripts/build-spx-gnss.py --target host-test

# ARM64 cross-compile + ELF verification
python3 scripts/build-spx-gnss.py --target arm64 --output out/spx-gnss

# Both host tests and ARM64 build
python3 scripts/build-spx-gnss.py --target all --output out/spx-gnss
```

Daemon runtime options:

```sh
spx-gnss-daemon \
  --socket /run/spx-gnss.sock \
  --socket-mode 0660 \
  --mode nmea \
  --interval 1000 \
  --session-id 1
```

Pass `--mode json --enable-position-report` to also subscribe to `QMI_LOC_EVENT_POSITION_REPORT_IND` (`0x0024`) and emit single-line JSON fixes.

## Integrating with Android GNSS HAL / `LocationManager`

1. **Daemon launch**: Start `spx-gnss-daemon` as a non-polling `init` service once QRTR is available (the daemon opens `AF_QIPCRTR` lazily on first client connect, so it will not race MPSS boot or keep the modem awake while idle).
2. **HAL bridge**: Implement an Android `android.hardware.gnss` AIDL/HIDL HAL (or minimal NMEA HAL wrapper) that connects to `/run/spx-gnss.sock` strictly inside `IGnss::start()` and closes the socket immediately inside `IGnss::stop()`.
   - Closing the socket triggers an immediate `QMI_LOC_STOP_REQ` in `spx-gnss-daemon`, preserving s2idle power without wakelocks.
   - Use `--mode nmea` for standard NMEA-0183 forwarding (`IGnssCallback::gnssNmeaCb` + `spx-nmea.c` fix extraction for `gnssLocationCb` / `gnssSvStatusCb`), or `--mode both --enable-position-report` to take direct WGS-84 fixes and circular horizontal accuracy from `QMI_LOC_EVENT_POSITION_REPORT_IND`.
3. **Permissions**: Match `--socket-mode` and Unix socket ownership/SELinux label to the GNSS HAL service (`gps` / `system` group).

## Validation Boundary

- **Verified**: Host unit-tested under `gcc -Wall -Wextra -Werror -fsanitize=address,undefined` (QMI LOC v2 exact TLV byte vectors, NMEA-0183 checksum/multi-constellation parsing & fuzz safety, on-demand session controller, Unix socket lifecycle) and cross-compiled cleanly to AArch64 ELF with `aarch64-linux-gnu-gcc -Wall -Wextra -Werror`.
- **Not yet validated**: Live hardware execution against SC8180X MPSS on Surface Pro X SQ1/SQ2 (TTFF, cold/warm fix accuracy, MPSS restart recovery, and long-term s2idle power draw remain to be hardware-validated on device).
