# SQ1 suspend/wake checkpoint — 2026-10-31

**Work in progress, not a working suspend feature.** The user requested saving
this checkpoint on a separate branch. No default build or normal boot entry is
changed by this commit. It is not permission to test sleep on another device.

## Latest result — 2026-10-31 (supersedes the initial checkpoint below)

The operator returned and reported a short power-key press. Readback showed
the same boot and core Android processes, an increased power-key IRQ count,
Android Asleep with reason `power_button`, and no completed real-sleep request.
That initial press only switched off the screen; it was not a wake test.

### First coordinated request: early SSAM wake, not successful sleep

The prepared one-shot script was uploaded with matching SHA-256 and executed
once with `pm_test=none`. It consumed its own task's token and returned zero.
The kernel success counter increased 0 to 1, but the trace did **not** contain
`machine_suspend`: an IRQ arrived during noirq device suspend, before idle.

- Kernel log: `Triggering wakeup from IRQ 172` on this boot.
- IRQ mapping: `ssam_wakeup`, TLMM GPIO 180; not the separate PMIC power key.
- `suspend-to-idle` and `resume from suspend-to-idle` were only about 2 microseconds
  apart; there is no evidence of CPUs entering the actual idle wait.
- The power-key count remained 2 before/after. Boot ID, system_server,
  SurfaceFlinger and Bluetooth bridge were unchanged. Touch transport reported
  ready, and the trial token and tracing were cleared on return.

The local `power-input-boot` script explicitly forces the Surface Aggregator's
`power/wakeup` to enabled. Its live hash matched the archived script. In contrast,
the matching kernel's `ssam_irq_setup` documentation and `ssam_irq_handle` TODO
state that EC event filtering is unimplemented: ordinary queued events can cause
spurious wake. `core.c` therefore defaults the device's wake policy to disabled.
This identifies a concrete conflicting policy, **not proof of the cause of the
remaining black screen**. No specific EC event (battery, lid, etc.) was decoded.

### Second coordinated request: black screen, post-test state unknown

A separate single-run script was launched to temporarily set only the SSAM
wake attribute to disabled, leaving PMIC power-key wake enabled. Its cleanup
restores the original attribute after return. It did not replace a boot script,
kernel, DTB, initramfs or menu and did not unload a module.

The SSH session ended with connection reset (client exit 255); the only received
stdout was the expected Image hash check. The remaining script output was
directed to the ESP and has **not yet been recovered**. The operator explicitly
reported short-pressing after this test and still seeing a black screen.
Subsequent reads failed: ICMP timed out; TCP connected but SSH closed before
key exchange. These observations do not establish that the kernel was alive,
dead, rebooted, or had reached the idle wait. The second script's final attribute
state, PM return, cleanup, trace and wake IRQ remain unknown.

The operator was asked to long-press recover and use the original default boot
entry. **Recovery has not yet been confirmed.** Do not label this test a pass or
restart it automatically. The temporary sysfs setting does not survive a board
reboot; no durable SSAM change was installed. If there was no reboot and cleanup
did not run, its runtime state must be read back before further work.

Patch 08 archives a proposed copy of `power-input-boot` removing its forced
SSAM enable block while preserving all other code. It is **not deployed** and
is **not a verified suspend fix**. Fixture baseline/rollback force `enabled`;
the modified copy leaves the driver's `disabled` default. Exact source rollback
and patch reconstruction passed. Do not promote it merely because it removes an
early wake: doing so exposed an unresolved black-screen path in this experiment.

Local evidence extends the same four-role set under `out/sq1-suspend-20261030/`:
`real-trial/physical-real-1.*`, `real-trial/wake172.*`, and
`ssam-wake-policy/physical-real-2.*`, reconnect results, source roles and the
second test script. Raw logs and host/device identities remain private.

**Next action:** confirm recovery and actual current IP/boot identity, retrieve
the prior ESP test and kernel-live files before any new test, and reconcile them
with the operator's black-screen report. If the frozen logger did not persist
late-stage data, record that limitation instead of inventing a hang location.
Then choose instrumentation for the remaining entry/resume failure. No working
sleep/wake, repeated-cycle stability or default integration is established.

## Confirmed progress

- The diagnostic kernel retains read-only `suspend.screen_only=1` and
  `nohibernate`. An explicit `screen_only_test=1` boot allows only the freezer,
  devices and platform debug stages. Passing these stages is not real sleep.
- A bounded device-list prefix experiment isolated a QCA Bluetooth suspend
  callback returning `-ETIMEDOUT`. Prefix counts include entries without a
  hardware callback; they are not counts of independently validated devices.
- The tested 2 Mbps UART DTB alone did not fix firmware setup: the historical
  `qca_tlv_check_data` parser skipped the actual tag 17 inside a type-4 NVM
  container. The patch adds container handling and bounds checks, based on
  upstream commit `a112d3c72a227f2edbb6d8094472cc6e503e52af`. Only the private
  download copy changes; firmware files are not distributed here.
- The original bridge unconditionally issued `HCIDEVDOWN`, provoking a second
  firmware setup after the first had succeeded. The candidate instead binds
  HCI_USER during HCI_AUTO_OFF, keeps bounded busy retries, and removes only the
  helper's extra three-second delay. No active QCA module was unloaded; persisted
  Android Bluetooth choice was retained.
- With the parser, matching 2 Mbps DTB and direct handoff, subsequent diagnostic
  boots had Bluetooth ready. The formerly failing QCA callback returned, the
  full devices stage completed, and then platform including late/noirq completed
  with the expected five-second debug delay. The platform test returned zero,
  success counter increased 0 to 1, failures stayed zero, and boot ID and Android
  core process IDs stayed unchanged. USB controller reinitialization and touch
  transport reopen messages were observed; software readiness is not a physical
  touch or display acceptance test.

## Initial 2026-10-30 checkpoint (historical)

The isolated candidate changes the `enter_state` screen-only rejection branch
to also consult `sq1_trial_take`. It adds:

- Boot-only `suspend.screen_only_trial=1`, default false.
- Root-only `trial_pid`, which can be armed only for the writing task itself.
- A five-second token deadline and one-time consumption under the system
  transition mutex. Only s2idle with `pm_test=none` qualifies. Other processes
  cannot consume the token; `screen_only` remains read-only and enabled.

The kernel compiled, QEMU checks passed, and the device booted this candidate.
The unarmed physical request was rejected as intended; its failure-counter
increment is a policy rejection, not a failed real-sleep attempt. Android and
Bluetooth remained ready. At that checkpoint the token was zero and no real
sleep had been invoked; the October 31 attempts and unknown current state are
recorded above.

QEMU uses an intentionally failing fake device callback to check the real
`pm_test=none` gate without entering idle. Unarmed, wrong-owner, other-caller,
duplicate, expired-token and missing-opt-in cases were rejected. **These results
do not validate physical wakeup or energy savings.**

The current `surface_rtc` driver has read/set time operations but no alarm
operation. Do not promise RTC timed wake. The power-key wake source is enabled,
but still needs a real sleep/wake test. A separate tracing instance was smoke
tested with the boot clock, `power:suspend_resume`, and the observed power-key
IRQ filter, then turned off. No global continuous tracing was enabled.

At that checkpoint the user was not available for physical operation; the later
coordinated attempts supersede that blocker. Do not start actual sleep unattended,
repeat recovery reboots, or disable the screen-only guard globally. Normal and
rescue entries remain intact.

## Archived source series

`patches/sq1-suspend-wip/series.json` binds each patch to exact before/after
SHA-256 values. Files are historical diagnostic baselines, not arbitrary current
upstream Linux sources. Apply only on isolated copies with matching hashes:

| Order | Change |
| --- | --- |
| 01 | Guarded PM debug-stage whitelist |
| 02–03 | Bounded device-list prefix and cross-file permission predicate |
| 04 | Single-owner, expiring one-shot s2idle trial gate |
| 05 | QCA type-4 NVM container parsing and bounds |
| 06–07 | Direct HCI_USER handoff and prompt helper bind |
| 08 | Proposed removal of forced SSAM wake enable; not deployed, not a proven fix |

The historical helper predates newer Bluetooth input-watcher work on main.
**Do not replace the current helper wholesale with the historical version.**
This branch archives the exact tested delta without integrating it into default
builds or dropping newer unrelated fixes. The DTB JSON describes one exact
binary's `max-speed` change; offsets must never be used on an unknown DTB.

### Offline verification

Requirements: Linux/WSL, Python 3.9+, GCC, GNU patch, and the exact original
source files. Supply a directory with this layout (hashes are in `series.json`):

```text
BASELINE/
  kernel/kernel/power/suspend.c
  kernel/drivers/base/power/main.c
  kernel/drivers/bluetooth/btqca.c
  probe/sq2-hci-bridge.c
  probe/bluetooth-framework-boot
  probe/power-input-boot
```

```sh
python3 scripts/check-sq1-suspend-wip.py BASELINE > verification.json
```

The checker validates hashes, applies all eight patches to a temporary copy,
checks each resulting hash, reverses the complete series, and verifies both
the restored copy and untouched originals. It also compiles the extracted
policy functions and a mocked bridge transport:

| Same input | BASELINE | MODIFIED | ROLLBACK |
| --- | --- | --- | --- |
| Trial gate, first/second call | reject/reject | allow/reject | reject/reject |
| HCI mock | forced down, timeout | no down, bind after two busy replies | forced down, timeout |
| SSAM policy, driver initially disabled | forces enabled | leaves disabled | forces enabled |

All checker assertions passed at export. The mock's expected baseline/rollback
exit is 1; the modified success exit is 0. Modified fatal/busy exhaustion remains
an error and never forces down another owner. This is source/fixture evidence,
not three physical device boots. Original guard-whitelist roles additionally
retain their baseline/reverted rejection and modified three-debug-stage behavior.

## Local resumption evidence (intentionally not published)

The ignored `out/sq1-suspend-20261030/` directory retains the original four roles
`MODIFIED_FILE.c`, `DIFF_FILE.patch`, `VERIFICATION.txt`, `ROLLBACK.sh`, and the
`prefix-diag`, `bt-nvm-parser`, `bt-handoff`, `full-pm-debug`, and `real-trial`
supplements. `status.json` records the current device identity, boot ID, process
IDs, menu hash, guards, and next action. Raw logs, complete images, firmware,
initramfs members, passwords, SSH keys and identity-bearing deployment scripts
remain ignored and are not part of this commit.

Useful artifact identities:

- Installed trial Image: `036fa25bf22c52048e27b7f7cbec90449afc7787a003133a226aa2aec0e6a471`.
- Earlier prefix Image: `1de576a7d3782d769a9eb199bae8192bd71b81e48b65c7496bbe55708493194e`.
- Trial source: `88174ab9e26e517a89b9e08d1a43e101e05744a66ab8cf41a8c8aa221517477c`.
- Matching 2 Mbps DTB: `d08c0dded6907c0ada0ec7a7a3b219e7156e2f40b800c7171e7f733f486c2d5c`.

The old diagnostic build directory contains a rollback Image; use the archived
hash-identified Images, not whichever build output happens to be newest.

### Resume procedure and completion boundary

1. Obtain current operator availability for the power-key wake and screen/touch
   state. Revalidate local checkpoint against the live device; do not assume a
   saved PID, IP, IRQ, boot ID or disk identity is still current.
2. Both prepared scripts have now been invoked once. Preserve their logs and
   one-shot markers; do not reuse them blindly. After recovery and evidence
   review, any further test needs fresh boot/IRQ/process preconditions and
   coordinated physical wake. There is no automatic timer.
3. Capture actual machine-suspend entry/exit and duration, wake IRQ, return code,
   unchanged boot ID, Android/BT/storage/display/touch recovery and user-visible
   result. A fast abort or a manual long-press reboot is not successful wake.
4. Diagnose any failure before broadening tests. Only then test repeated real
   sleep/wake and normal user-driven integration. No normal-default promotion
   or claim of repaired suspend is justified by the current checkpoint.

Earlier devices-wrapper exit 1 came from an absent log-string assertion despite
a successful PM return; the following read-only audit confirmed success. An
initial platform EPERM was the prefix diagnostic gate's intentional TEST_DEVICES
restriction, not hardware failure. `video_ready=0` logs are a successful return
code, not a boolean reporting no image. Historical RAM-marker retention attempts
were inconclusive; durable logging here means files on the ESP, not EFI-variable
pstore. These distinctions must survive resumption.
