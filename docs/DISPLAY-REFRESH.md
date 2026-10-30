# SQ2 display refresh investigation — 2026-10-29

## Conclusion committed / build-profile integration — 2026-10-30

The selected experimental default remains **5.4Gbps×4 / native ~65Hz**.
Full Android, touch and operation passed the user's short observation after the
correct software-EDID boot. Stock panel/link **2.7Gbps×4 / native60** remains a
separate selectable default and rescue path. `--display-profile 5.4-65|2.7-60`
sets the image's next-boot default, not a live Android display toggle.

The common initramfs retains the pinned original msm; only the 65 entry appends
V6 module080cceff… and EDID83101509… plus the original credential member last.
Stock/rescue never reference that addon or an EDID override. No EEPROM write,
automatic reboot, bandwidth-capacity claim or unrelated driver retuning is added.
The tracked menu template, exact payload delta and packed-image deployment policy
are covered by host tests. This integration is not a new device boot test.

Full Android70 failed visually;75/120 were not usable,90 only had a short standalone
colorbar observation. The2560×1706@60 negative control was black despite a lower
timing budget, and native65 recovered in the same boot. **No physical bandwidth
ceiling or maximum usable resolution at70Hz was established.** Do not add these
failed/inconclusive candidates to normal boot. Long-term stability, power and
external frame-skipping measurements remain pending. Build usage is in BUILD.md.

## Latest: Android70 failed visually; lower-resolution control also black

User requested full Android70 output, not just KMS color bars. A software EDID
70 mode at414750kHz was one-shot booted as `88d5ec48…`: DRM and Android70,
5.4×4, effective EDID485249ff…, V6 module080cceff…, guardY, completed Android,
SF/HWC running. User reported abnormal screen/operation. This is **not a pass**.
The65 default was not promoted to70. One-shot consumed0; original65 menu was
restored. User manually recovered, independently verified boot `adab52a4…`
with real65/385120 and Android ready. No autonomous crash or successful70
desktop is inferred from the software report.

User then explicitly selected fixed70Hz/lower **real output resolution** to
test a suspected2.7×4 bandwidth ceiling. The hypothesis is not established:
2.7×4 has nominal8.64Gbit/s payload with8b/10b coding, while the working native65
mode has a9.24288Gbit/s pixel-clock×24 timing budget. Driver-reported5.4×4 is
not an external physical-link throughput measurement; visible output alone
does not prove no skipped frames. Timing budgets below are not measured payload.

`build-sq2-bandwidth-edid.py` builds a software-only matrix from verified65
EDID83101509…. Native65 stays first/preferred; the other three descriptors
become test DTDs. No panel EEPROM write. Matrix EDID8c66f21b…, addon590b1419…,
temporary six-entry menua490d132…; init/base/kernel/DTB unchanged, credentials
last, no automatic reboot hook. One-shot request consumed before loading.

| Mode | Pixel clock kHz | RGB24 timing budget Gbit/s | Observation |
|---|---:|---:|---|
| Native2880×1920@64.999156 |385120|9.24288|Working baseline and restored mode|
|2560×1706@59.998135 control|283160|6.79584|Real Android/KMS switched; user explicitly said black screen|
|2560×1706@69.999237|330360|7.92864|Enumerated only; not attempted after control failed|
|2700×1800@69.998662|366170|8.78808|Enumerated only; not attempted after control failed|

Matrix boot `59556813-0cf8-4d11-90d7-056c6928ebc7` exposed all4 Android modes.
Actual `cmd display` help supports nonpersistent `STORE_MODE=false`; use that,
not `wm size`. Control command was
`cmd display set-user-preferred-display-mode 2560 1706 59.998135 0 false`.
Android reported real2560×1706/59.998135, DRM active2560×1706@60/283160,
pixel readback283160000 and link540000×4. Display state was explicitly ON before
the user's black-screen confirmation, so idle screen-off was not counted as
the mode failure. `get-active-display-mode` was advertised in help but returned
Unknown command; snapshot was corrected to dumpsys+DRM without repeating setter.

The below8.64 budget control still failed. Therefore black output is **not
sufficient evidence of exceeding a2.7×4 capacity limit**. Non-native timing
acceptance or that mode's video-transport configuration is still a confounder.
Neither a physical bandwidth ceiling nor a highest working resolution at70Hz
was established. Do not report the unexecuted70 modes as failed/passed, and do
not continue narrowing a throughput threshold from this invalid control.

`cmd display reset-user-preferred-display-mode 0` returned0 and restored
native65 in the same boot. Verified DRM385120, Android64.99915, user preference
null, screen_onlyY; user confirmed normal output again. system_server/SF
PIDs2222/2030 remained, with no service/board restart between mode switches.
Temporary matrix menu was backed up and atomically restored to the original
five-entry65 default, full hash37bdce73…. Matrix EDID remains only in the current
boot; next normal boot uses the original single65 EDID. No extra reboot issued.

Same four-role paths under `control-v5/` are retained; `bandwidth-test/` extends
the ledger with the EDID delta/rollback65, mode1/4/1 copy transaction, all
checksums0 and exact restored hash/reconstruction, plus the real65→black
low-resolution60→normal65 cycle. Source AUX3/0/3 and rate540000 remain unchanged
host results. Builder DTD timing/polarity unit test passed. Raw EDID, packages,
device logs and private credentials remain ignored; none is published to Git.

## Current default65 R2: reboot software and user confirmation passed (short run)

User explicitly requested reboot verification. First reboot `4fb46847…`
loaded the expected c754 module and5.4×4, but DRM and Android remained60Hz.
The user confirmed that desktop/touch were normal in that60Hz boot. Inspection
identified the missed path: eDP bridge does not set DRM_BRIDGE_OP_MODES and
built-in `panel_edp_get_modes` supplies EDID modes directly. The earlier MSM
get_modes host test did not exercise the real eDP boot callback. That approach
is archived, not described as a working65Hz mechanism.

R2 uses the kernel's existing DRM firmware-EDID path, scoped to eDP-1 only in
the default65 entry: `drm.edid_firmware=eDP-1:edid/sq2-native65.bin`. It writes
no panel EEPROM. `build-sq2-edp-edid65.py ORIGINAL OUTPUT` builds a software
override from the verified128-byte physical EDID. Only bytes54/55 (DTD clock),
78/81 (software mode-range proposal) and127 (checksum) change. Original data,
geometry, porches, polarities and all other fields are preserved. These range
values are an override proposal, not an OEM capability claim. The base EDID's
10kHz clock granularity yields385120kHz /64.999156Hz, nominal65Hz. No false
exact385125 encoding is used.

R2 module is the already verified cached-only V6 `080cceff…`, not the missed
c754 callback candidate. Addon
`527677975b09c31a7aeb100f7cee85058e68020b96abb5f4785ec3a169522040`
changes only msm plus the EDID file/parent directory. Original init, kernel,
DTB and c5 baseline remain unchanged; lab SSH credential addon remains last.
No automatic reboot/rollback hook or normal-boot diagnostic networking/logging
is introduced. Original four60Hz/Mesa/rescue/shutdown entries remain unchanged
and have no EDID override. Installed default65 menu SHA256:
`37bdce73bb6c96410579e5099da8adf87acba9614564bf1d935e24927dac1356`.

Second user-requested corrective reboot `9fac091e-49cf-4015-bd2f-fa4321693745`
confirmed the cmdline/DRM firmware parameter. Installed firmware and effective
connector EDID both SHA256
`83101509b096d6ddfc83e96bd08917d4989f78b80e2549489bd12f8d14e5a014`.
Observed active DRM2880x1920@65/385120kHz, selected5.4Gbit/s×4, pixel clock
driver readback385120000, Android default/supported/render/vsync64.99915Hz,
boot_completed1, SF/HWC running, screen_onlyY/nohibernate. At uptime275s the
same boot and system_server/SF PIDs2190/2022 remained. These are driver/software
readbacks, not external physical clock or no-skipping measurements. **The user
subsequently explicitly confirmed normal screen, touch and operation in this
corrected second boot**. No further
reboot is scheduled; long-term stability/power and external frame skipping are
not verified. This is device-local experimental boot configuration, not global
default-build integration.

The original four-role paths are reused for the effective cached-only V6
source; the failed V7 get_modes source/diff is supplemental history. Source
BASELINE/MODIFIED/ROLLBACK diagnostic AUX counts3/0/3, all exit0, restored hash
and patch reconstruction match. `default65-r2/` extends the ledger with EDID
BASELINE/MODIFIED/ROLLBACK60/64.999156/60Hz, checksums0 and exact binary-patch
reconstruction/rollback hash. These are copy tests, not three device boots.
The builder's two host tests cover exact allowed-byte delta, unchanged source,
no overwrite and incorrect-panel/timing/checksum rejection. Raw EDID, binary
packages, reboot logs and private credentials remain ignored, not in Git.

## Latest: user-requested default65 installed, not yet boot-verified

The user explicitly requested 5.4-Gbit/s × 4 / 65 Hz as the default, overriding
the earlier test-only boundary. They also requested fewer reboots and manual
recovery: no automatic board-reboot hook is included in subsequent candidates.

65-Hz color bars were user-confirmed normal; 130 vblanks took 2.003758 s and
native60 restoration succeeded. Android display-service handoff then lost SSH;
recovery to the original module was verified afterward. In manual-recovery
boot `5bbae357…`, 70 and75 were tested in one DRM session with native60 between
them: 140/150 vblanks took 2.000437/2.003712 s. User confirmed70 normal and75
abnormal. Software handoff reported success with the test framebuffer retained
until Android takeover, but the user still reported an abnormal desktop.
Therefore it is **not a validated desktop-recovery fix**, nor is75 a pass.
User manually recovered to boot `9bff4bf2…`, original msm, guardY and completed
Android. Do not label that manual recovery an autonomous crash.

`patch-sq2-edp-default65.py STAGE` accepts the exact cached-only V6 source and
adds a scoped preferred-mode adjustment in `dp_panel_get_modes`. It requires
SurfaceProX, eDP, LGD0555 / `LP129WT112684`, four lanes and rate-table maximum
at least540000; only the exact original355500-kHz/3000×1975 native timing and
polarities are eligible. It changes pixel clock to385125kHz, not EDID bytes or
porches/PHY parameters. DRM probe helper recomputes crtcinfo after get_modes.
Source SHA256 `7ec1c028…`; module
`c754e4142b796143cc92ddc02bb1ea25879d0029ef952663394fec0c30245798`.
Original dependency/import guard remains; all608 imports have live exports,
still not a full Module.symvers/modpost validation.

Independent addon `7cd97a1b…` is based on the unchanged current `c5c64cda…`
image. Last-entry audit changes only msm; init/kernel/DTB and other files stay
unchanged, lab credential addon last. Original four menu entries remain byte
identical. Added default entry `sq2-edp-default65` uses the original base plus
the new addon. There is no one-shot/env override in this final default menu.

Identified-device backup, gzip/hash checks, sync and atomic installation passed.
Installed GRUB full SHA256:
`246fa9fdd86c558c9b83586849df7665369f14d220e36fd154f3189a439de9e6`.
Readback confirms default65 and five entries, retaining Qualcomm original60,
Mesa60, rescue and shutdown. **No reboot was issued for this installation**:
current boot remains `9bff4bf2…` with original module/60Hz. Full Android65 boot,
frame skipping and stability remain unverified until a later user-controlled
boot. If abnormal, choose original Qualcomm60; do not overwrite that fallback.

The same four-role files under `out/display-refresh/control-v5/` are updated to
this latest source candidate; earlier V5/V6 and native-probe records are retained
as supplements. Actual extracted get_modes host tests observe native clock
BASELINE/MODIFIED/ROLLBACK355500/385125/355500kHz, all exit0; wrong connector,
link, panel, machine, timing and missingEDID remain unchanged. Original hash
and patch reconstruction match. Supplemental menu-copy checks observe default
Qualcomm/default65/Qualcomm with4/5/4 entries and successful GRUB syntax checks.
These host tests are not physical Android65 boot evidence. Private inputs,
package audit, installation stdout and readback stay in ignored
`out/display-refresh/default65/`.

## 2026-10-30: current-device V5/V6 and direct KMS feasibility tests

The current test device is a separately identified Surface Pro X / Microsoft
SQ2, with a Toshiba 128-GB SSD and a different ESP/data identity from the old
512-GB lab device. **Do not run the old fixed-identity installer on it or remove
its checks.** Its EDID is LGD product 0x0555, text `LGD_DV1.0_` /
`LP129WT112684`, 128 bytes with valid checksum, native 2880x1920@60, range
30–60 Hz and 360-MHz maximum pixel clock. Receiver reads independently confirm
the same eDP rate-table slots through 5.4 Gbit/s, four lanes, and no advertised
DSC/FEC. The local current boot-image baseline is `c5c64cda…`; it is not the
old `5ba59bd9…` image. Kernel, DTB, non-display files and the final lab SSH addon
are preserved. Original Qualcomm/Mesa/rescue/shutdown menu entries are retained.

- **V5 2.7-Gbit/s table control:** rebuilt module exactly matches
  `bfc7d6459b71091f3c8bfcc4cb41107c5f8d3d102ea800bc10ee07f32bb24caf`.
  Actual boot `2b4136a0…` had Android completed, guard Y, BW_SET=0,
  RATE_SET=3, lane status 77/77, aligned, native 60 Hz. User confirmed normal
  display/operation. The consumed one-shot request returned to original msm
  `f69c91e7…` and normal Android in boot `5ddeed58…`.
- **V6 cached-only diagnostics:** `patch-sq2-edp-cached-diag.py STAGE`
  removes only diagnostic AUX reads from `sq2_edp_diag_snapshot`; capability
  and training transactions remain. It accepts pinned isolated V4/V5 source
  hashes and rejects repeat/unknown inputs. Uncapped V6 source hash
  `7d87f794…`, module
  `080cceff71f5b6168bfc89ed372a48e4ac783cf09fa9500f573411651a54f263`.
  Dependencies restored from the original module; all 608 undefined imports
  have live exports. Missing Module.symvers remains a full-modpost limitation.
  Current-base combined image `45bdb3e9…`, addon `873bf364…`; last-entry audit
  changes only msm and test init hook, credential addon last.
- **V6 5.4-Gbit/s / 60 Hz:** boot `746b84ba…`, module hash matched,
  training/PHY/OPP/pixel/video return codes zero, selected link 540000 × 4,
  pixel clock 355500000, user confirmed normal desktop/operation. This is a
  current-device result, **not proof that removing AUX reads alone fixes the
  old device's V4 failure**. Boot-default image was not promoted.
- **90-Hz KMS test:** after a successful 60-Hz tool/control/recovery test,
  stopped only SurfaceFlinger/composer, used an isolated dumb-buffer color-bar
  framebuffer and original timing totals/polarities, pixel clock 533250 kHz.
  KMS returned 0; 180 vblanks took 2.005761 s; cached driver and clock-tree
  readback report 90 Hz / 533250000 Hz / 5.4 Gbit/s × 4. User initially
  reported abnormal/black output, then explicitly clarified that the sustained
  **90-Hz** color-bar window became normal, not merely its final 60-Hz restore.
  Native60 restore and Android services succeeded in the same boot. This is
  short probe/visual feasibility, not Android 90-Hz mode integration, extended
  stability or external optical frame-skipping verification.
- **120-Hz first dispatch:** SSH failed and no persistent runner directory
  exists; cannot establish that the switch began. Subsequent normal boot
  `3739d506…` matched original msm and guard Y. Do not call that an observed
  120-Hz mode failure or confidently attribute its reboot to a specific timer.
- **120-Hz isolated retry:** new V6 boot `eac96f1b…`, before the original boot
  watchdog deadline, no earlier KMS timers. Phase records are synchronized to
  ESP, with a persistent restore marker and explicit watchdog cancellation.
  KMS set 711000 kHz returned 0; 240 vblanks took 2.003816 s. User reported
  sustained black/corrupt/flickering output. Native60 KMS restore returned
  `-1 / errno=7 (E2BIG)`; SSH then timed out. This timing is **not usable**.
  Persistent phases prove MODE_TEST_RETURNED_EXIT_1 at uptime 93.99 s,
  then WATCHDOG_FIRED_RESTORED_MARKER_ABSENT at 139.78 s (armed 64.78 s).
  Thus fallback followed failed restore, not premature interruption of a
  successful 120-Hz test. Do not retry this timing unchanged.
  Recovery verified in boot `02596cb6…`: original msm full hash matched,
  Android completed, guard Y, active 2880x1920@60 / 355500 kHz; user confirmed
  desktop restored. Original four-entry GRUB was atomically restored and
  re-read with original full hash
  `a7cc581f87992b328426c8be1ac724635061957106b132d061e22aa03a205805`.
  Current default image/kernel/DTB remain unchanged; experimental files and
  backups are retained but no test request is active. No further reboot was
  needed for menu restoration. E2BIG's exact driver rejection site and the
  receiver's 120-Hz limitation are not yet isolated.

At 24 bpp and unchanged 3000×1975 timing totals, 90/120 Hz need
12.798/17.064 Gbit/s and 533.25/711 MHz pixel clocks. Both fit the nominal
17.28-Gbit/s HBR2 payload ceiling arithmetically (120 has only 1.25% margin),
but exceed this EDID's 60-Hz / 360-MHz declaration. Software acceptance and
vblank alone do not establish receiver scanout or no dropped frames. No
voltage, panel firmware, DSC capability or permanent EDID changes were made.

Native probe: `probe/graphics/sq2-edp-mode-test.c`. Compile statically with
`aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -Werror -I/usr/include/libdrm`.
`--inspect` is read-only; `60|90|120` are explicit lab modes, never boot hooks.
Do not run a mode without a separately armed restore/reboot watchdog and
stopped display services. After a successful run restart composer/SF and
independently inspect active native60. `tests/sq2_edp_mode_timing_test.c`
checks exact totals/polarities/clocks and rejection before DRM open, with host
ASan/UBSan; it does not operate hardware.

Ignored evidence: `out/display-refresh/`, including current-base backups,
V5/V6 audits, device logs and KMS stdout. The same four-role set is retained
under `control-v5/`: latest MODIFIED_FILE/DIFF describe cached-only V6, prior
V5 cap source/diff remain supplemental, VERIFICATION is append-only, and
ROLLBACK.sh embeds pristine source bytes. Actual-function host tests observe
diagnostic AUX call counts BASELINE/MODIFIED/ROLLBACK = 3/0/3 with unchanged
rate 540000/index6, all exit0; restored hash and patch reconstruction match.
This source transaction is distinct from the physical-device tests above.

## V4 training evidence and pending V5 control

Subsequent rollback verification: SSH reconnected, boot_completed=1,
screen_only=Y, and the original msm full SHA256
`f69c91e754e2a0d66cd195549d970973ed81b4c6f1bbfcbc3723f80a26414633`
matched. The user then confirmed desktop/lockscreen recovery, closing both
the SSH/module and visual recovery gaps below. V5 remains undeployed.

V4 adds cached PHY/clock/training/video return codes and early DPCD snapshots,
without changing PHY voltage, PLL constants or the native 60 Hz timing.
Module SHA256: `839b5afd74df7eaf6d8a3e891916cf87031906aee61ef1c788a84681fe09b107`.
The actual boot recorded PHY configure/power/OPP returns 0, training step 2
return 0, pixel request/readback 355500000 and video-ready return 0.
Receiver registers independently read back BW_SET=0, LANE_COUNT_SET=0x84,
RATE_SET=6, lane status 0x77/0x77 and alignment/status 0x81. Thus the initial
5.4 Gbit/s four-lane training and alignment passed; this does not establish
stable video output, physical clock measurement, or high-refresh support.
MSA was MVID=43055/NVID=65400. Software clock reporting was 540 MHz link
and 355.5 MHz pixel. Encoder vsync samples were 0,9,9,10,10, indicating that
normal sustained scanout did not follow. Later AUX/debug reads blocked or
timed out. Repeated diagnostic AUX access is a possible confounder; do not
attribute that later stall solely to receiver hardware.

Raw evidence was retrieved to ignored out/display-refresh before an explicit
agent-requested sync/reboot. User subsequently reported the device still
starting; this latest rollback has not yet been verified over SSH. Do not
describe that reboot as an autonomous crash or claim desktop recovery yet.

V5 is built locally but NOT deployed or tested. It retains table negotiation,
caps the isolated candidate to 270000 (original table slot 3), and limits AUX
snapshots to the first three collector iterations. This control separates
rate-table negotiation from the faster link/stream path. Module SHA256:
`bfc7d6459b71091f3c8bfcc4cb41107c5f8d3d102ea800bc10ee07f32bb24caf`.
Combined initramfs SHA256:
`3909460384a047f97d3c0fb07f03d0d6a522c06c734928b6ccf23ea482df3ae4`.
Current device debug entry still references V4; default closed-Vulkan and
Mesa images are unchanged. Only three menu entries remain. No 90/120 Hz
mode has been attempted.

The independent 240-second watchdog is separate from the collector so a
blocked AUX read does not serialize its timer. Normal boot has neither
collector nor diagnostic Wi-Fi. The one-shot request is consumed by GRUB
before loading the candidate. Existing kernel, DTB, screen-only guard and
last credential addon remain preserved.

Final recovery follow-up: after the user reported connectivity restored, SSH
verified boot_completed=1, original msm SHA256 f69c91e7… matching the full
pinned original hash, screen_only=Y, default closed-Vulkan command line with
no eDP test/debug flags, and consumed sq2_edp_once=0. Live display returned
to 2.7 Gbit/s × four lanes, 2880×1920@60, 355500 kHz, 24 bpp. This closes
the post-reboot verification gap recorded below. V3 logs were retrieved into
ignored out/display-refresh; no new mode switch or reboot was performed.

## Rate-table candidate and boot tests

**V3 real test reached Android boot_completed=1 and SSH. Candidate module
hash matched. Driver debug showed capability and selected link 540000,
four lanes, bw_code 20, lclk 540000000, unchanged 2880×1920@60/355500 kHz/24bpp.
User reported black/corrupt display. Repeated DPU vblank/commit/frame-done
timeouts were observed. This is a failed display test, NOT working 5.4-Gbit/s
output or proof of panel high-refresh support. No raw DPCD lane-status snapshot
was obtained during this boot.**

When the user asked about force reboot, SSH was available; link state was
saved to the test's ESP log directory, one-shot env was verified consumed
(sq2_edp_once=0), then an agent-requested sync + reboot returned toward the
normal entry. User subsequently confirmed normal desktop/lockscreen recovered.
SSH to the previously used address did not reconnect during the final checks,
so the last post-reboot module hash was not independently re-read. The earlier
two rollbacks had verified original module hashes. Do not label the final
agent-requested reboot as an autonomous crash. Do not promote v3.
Next diagnosis must separate clock/MSA/TU/video output from receiver training
status, with bounded logging and rollback. Do not retry the same black-screen
candidate as a refresh-rate test.

Work branch: codex/sq2-edp-link-rates. Candidate msm module SHA256
`c04dcf94f860286d6185947003fa9b5681353681c1d8ecfb9ea5f565e0ad720e`.
The patch is scoped to Surface Pro X eDP, parses the 1.4 rate table, filters
to PHY-supported standard rates without renumbering receiver slots, writes
BW_SET=0 plus RATE_SET=index, checks short AUX writes, and uses table-aware
downshift. It leaves all timings/EDID modes unchanged. Host C boundary tests
passed with ASan/UBSan; all 608 undefined imports have live exports.

Build uses a dedicated msm source copy, the matching thermal cache/config,
and an external-module include-path adjustment. Missing Module.symvers is
explicitly not a full modpost verification. Existing Image/DTB remain intact.

Isolated addon `c10e9dc9…` plus the unchanged default v8 image has combined
hash `3c5c7621…`. Final-entry audit permits only msm.ko and the test watchdog
hook in init; credential addon stays last. GRUB consumes its one-shot env
before boot. A 240-second watchdog reboots unless confirmed. ESP was nearly
full, so the 1 MB addon is a second initrd argument rather than a duplicate
196 MB image. Default/rescue entries continue to use the original files.

First boot did not reach a usable desktop/SSH; user observed scrolling boot
text. User confirmed automatic reboot and normal desktop afterward. Original
module hash, boot_completed=1 and screen_only=Y were verified on recovery;
pstore was empty. No candidate link state was obtained: this is NOT a proven
5.4-Gbit/s training failure or success. Candidate was not promoted.

Diagnostic retry was armed with sq2.debug=1 to persist startup logs. The user
then clarified that its saved diagnostic Wi-Fi profile is flyer-test and is
not the current network. Do not reuse that association profile for further
tests. Prefer bounded local boot snapshots and Android-owned saved networking.
Diagnostic retry also returned automatically to the original module and
boot_completed=1. Logs identify a pre-training module-load failure: missing
drm_sched_* and llcc_* exports because the no-Module.symvers build emitted an
empty .modinfo depends field. Availability of exports on an already running
system did not ensure their providers would load during boot.

`restore-sq2-msm-depends.py` now restores the hash-verified original dependency
list `gpu-sched,llcc-qcom,mdt_loader`, checking identical vermagic, unchanged
code/imports, and that the only added import is built-in of_machine_is_compatible.
It does not invent CRCs or a Module.symvers. Fixed module hash is
`6293fa5d5cc3c462b1cee41fbf8a1eadeb6cf7e7055489b7afaf1e5999be88ac`.
V3 addon hash `46fec013…`, combined image `7ddd4ad6…`; packer now requires
the dependency field. V3 has bounded local snapshots every 30 seconds for
at most 240 seconds, no diagnostic Wi-Fi. Hardware result is the failed
display test described above.

User subsequently requested exactly three entries: default closed Vulkan,
original Mesa and current eDP test. Rescue, shutdown and redundant v8 opt-out
menu entries were removed at that explicit request. Shared images were retained.
Ten obsolete/unreferenced initramfs files (1,496,280,091 bytes) were copied to
ignored out/esp-retired-20261029 and individually SHA256-verified before deletion.
Deletion completed; ESP available space increased to 1,486,316 KiB (29% used).
Image/DTB files are not part of that deletion list.

**Latest: raw DPCD reads supersede the earlier 2.7-Gbit/s receiver-maximum
inference below. DSC is not advertised, but the eDP supported-rate table
advertises up to 5.4 Gbit/s/lane. Current msm code ignores that table.**

## Raw DPCD result

One-shot read-only module built from matching configured kernel headers;
all ten undefined symbols checked against live __ksymtab exports, vermagic
6.3.8-sq2-android1 SMP mod_unload aarch64. Missing Module.symvers means this
was not a full modpost verification. Module v2 SHA256:
`a0ec57434f246cb44ebb88d572a2c77193f1db11bb609616c40ff49f3434402c`.

Nine 16-byte reads returned rc=16. No DPCD write API is used. The module
locates the existing exact dp-aux child through the platform device and
uses its public aux pointer and drm_dp_dpcd_read(), without rebinding.

- 0x000 = 0x14: DPCD 1.4; 0x001 = 0x0a: legacy 2.7 Gbit/s;
  0x002 = 0xc4: four lanes. Extended receiver block 0x2200 matches.
- 0x060–0x06f are all zero on both reads: DSC decompression support bit
  is clear. 0x090 is also zero (no FEC capability advertised).
- 0x700 = 0x04: eDP 1.4a.
- 0x010 supported link rates are little-endian 16-bit values multiplied
  by 200 kHz: 1.62, 2.16, 2.43, 2.70, 3.24, 4.32, **5.40 Gbit/s/lane**,
  then a zero terminator. This table invalidates treating the legacy
  MAX_LINK_RATE field as the panel's complete advertised maximum.
- 0x100 = 0x0a and 0x101 = 0x84: current legacy 2.7 Gbit/s × four lanes.
  0x115 is zero; current configuration is not using table-based rate selection.

Matching i915 intel_dp.c:2944–2969 demonstrates table parsing and selecting
DP_LINK_RATE_SET when this table exists. Current msm dp_panel.c uses only
legacy MAX_LINK_RATE; table-based selection needs actual driver work, not
merely changing a DT clock. No 5.4-Gbit/s training or overclock was attempted.
At that advertised rate the nominal payload ceiling would be 17.28 Gbit/s,
but it is not a tested working link, and EDID still only advertises 60 Hz.

Both probe loads were followed by successful unload; boot ID unchanged,
screen_only=Y, connector enabled. No boot files/settings/registers written.
Raw captures stay in ignored out/display-refresh/dpcd-read[-v2].txt.
Rebuild with scripts/build-sq2-dpcd-read.sh; while awake and after ABI/export
verification, insmod the temporary module, read its result parameter, then
rmmod sq2_dpcd_read. The sysfs result is a cached snapshot, not repeated I/O.

The next candidate is eDP rate-table negotiation support, not DSC. A successful
higher-rate link would solve transport capacity only; panel scan timing and
actual non-skipping high refresh remain separate, untested requirements.

## Earlier investigation (retained with the correction above)

User authorized progressive refresh testing with rollback. Read-only hardware
inspection found a bandwidth gate before mode switching; no overclock was applied.

## Live observations

- eDP-1 connected, only 2880x1920 advertised.
- EDID manufacturer LGD, product 0x0555; text LGD_MP1.0_ and LP129WT112684.
  These strings are not sufficient to identify a retail replacement-panel SKU.
- EDID range descriptor: 30–60 Hz vertical, 360 MHz maximum pixel clock.
- Active timing: 355500 kHz, horizontal 2880/2928/2960/3000,
  vertical 1920/1941/1946/1975; exactly 60 Hz; 24 bits per pixel.
- msm_dp debug reports link_info 270000 / four lanes, and actual link
  bw_code 10 / four lanes / lclk 270000000.
- No drm_dp_aux userspace device is exposed, so raw DPCD receiver capability
  bytes were not independently read. Do not equate current link rate with an
  independently proven physical receiver maximum.
- screen_only=Y. No display override, Android setting, boot image or disk was
  changed. Temporary debugfs mount removed after inspection.

## Bandwidth gate

At four lanes of 2.7 Gbit/s with 8b/10b coding, payload ceiling is 8.64 Gbit/s.
Current timing consumes 355.5 MHz × 24 = 8.532 Gbit/s.
Even ignoring all blanking, native 2880×1920 at 65 Hz and 24 bpp requires
8.626176 Gbit/s, leaving effectively no practical timing overhead. 70 Hz
already requires 9.289728 Gbit/s without blanking.
Keeping the existing timing totals requires 9.243/9.954/10.665/12.798/17.064
Gbit/s at 65/70/75/90/120 Hz respectively. All exceed the observed link.

Therefore no progressive live overclock was attempted: existing link/timing
cannot carry even the first 65 Hz step. This is a capability/bandwidth
assessment, not an observed high-refresh failure or a successful overclock.

Next prerequisite is reading receiver DPCD and matching running driver/DT
link limits, to establish whether higher link training is possible. Only then
build an independently timed restore mechanism and temporary mode test.
Lowering color depth/resolution is a separate quality tradeoff; it does not
prove this panel will scan above its advertised 60 Hz. Actual frame skipping
requires external visual measurement, not just a reported refresh counter.

## Receiver versus driver follow-up

Live kernel is 6.3.8-sq2-android1. Inspected the documented matching
linux-surface b86f786 source in the sq2-thermal build cache, plus the live DT:

- Enabled controller is ae9a000, compatible qcom,sc8180x-edp. The other
  two DP controllers are disabled. Legacy data-lanes contains 0,1,2,3.
- Its output endpoint has no link-frequencies property. Live OPP table
  includes 270/540/810 MHz link-clock entries; this is not a receiver guarantee.
- dp_parser.c:97–139 reads the endpoint maximum, otherwise defaults to
  DP_LINK_RATE_HBR2=540000 (5.4 Gbit/s per lane).
- dp_panel.c:75–84 derives link_info.rate from receiver DPCD MAX_LINK_RATE,
  then clamps to the parser maximum. Extended receiver capability selection
  occurs before this. No other assignment to the panel link_info.rate was
  found in this source directory.
- dp_debug.c prints panel->link_info.rate separately from the active
  link->link_params.rate. Both were 270000 on the device. Training fallback
  works on link_params; the capability field is not merely the selected
  clock for the current 60 Hz mode.
- PHY source includes 5.4 and 8.1 Gbit/s handling; simply adding a faster
  OPP or changing the DT default does not override a lower receiver claim.

Inference from this source and live DT/debug data: receiver-advertised
maximum is 2.7 Gbit/s per lane, not a DT cap at 2.7. Raw DPCD bytes remain
unread: CONFIG_DRM_DP_AUX_CHARDEV is disabled. No binary disassembly or
independent read of cached receiver bytes was performed, so this is a
source-based inference, not a raw register dump or physical overclock limit.

Native resolution at 90 Hz needs 11.943936 Gbit/s even with zero blanking
and 24 bpp; 120 Hz needs 15.925248 Gbit/s. Even 18 bpp at 90 Hz needs
8.957952 Gbit/s before blanking, exceeding 8.64. Thus reducing color depth
alone cannot deliver native 90/120 Hz on this advertised link. No live
overclock or forced unsupported link training was performed. Boot ID and
screen-only guard remained unchanged.

Driver testing caveat: dp_bridge_mode_valid() returns MODE_OK early for
eDP, relying on the panel mode list. Therefore a synthetic mode passing
that callback would not validate link bandwidth or panel acceptance.
