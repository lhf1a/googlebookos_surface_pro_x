# SQ1 EL2 suspend checkpoint — 2026-11-03

> 后续结果与当前收尾状态见 [SQ1/SQ2 通用休眠实验记录](SURFACE-SUSPEND-20261103.md)。下文为首轮历史检查点；后续 .91 显式睡眠失败，不能据此认定修复完成。

Branch: `codex/suspend-fix-5.94-20261103`. Workspace:
`/mnt/spx-workspace/chromebookOS`. The operator explicitly resumed suspend
research on the device at 192.168.5.94 and authorized isolated default boot
experiments with fallback. The live DT identifies this device as **SQ1**, not SQ2.

## Observed result

- A newly built EL2 + pinned KernelSU Next diagnostic Image booted the current
  full Android runtime. The operator confirmed reaching the desktop.
- `freezer`, `devices`, and `platform` each returned 0 with the expected
  five-second debug delay. Success advanced 0→3, fail stayed 0. These are not
  real-sleep passes.
- One `pm_test=none` request consumed the writing task's one-shot token.
  The boot-clock trace contains `machine_suspend[1]` begin **434.015903** and
  end **458.739687**, an interval of **24.723784 s**. Return was 0;
  success advanced 3→4, fail stayed 0.
- The same boot and system_server/SurfaceFlinger PIDs survived. The operator
  subsequently confirmed that the screen and actual touch both recovered.
- Do **not** equate this with a coordinated power-key wake: immediately after
  the kernel returned Android was still Asleep, SSAM IRQ 173 had increased
  0→1, and PMIC IRQ 216 remained 2. A later physical key interaction made
  Android Awake (last-wake uptime 605101 ms); PMIC count subsequently became 4.
  The SSAM event that ended the earlier kernel wait has not been decoded.
- The trace buffer retained both machine-suspend boundaries but a
  `timekeeping_freeze` event flood overwrote many per-CPU entries
  (24738 retained / 780043 written). This is not a low-power residency or
  energy measurement. Filter that flood before the next trial.
- Full-boot checks included the physical Surface-Pro-X ALSA card,
  `AUDIO_READY`, active ADSP `attached`, FUSE emulated storage, and the
  **actual running** AIDL audio executable SHA
  `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`.
  The unused vendor audio.primary library has a different hash and is not
  the adopted HAL. No new acoustic acceptance is claimed.

## Current safety and boot state

The original two-entry menu has been restored byte-for-byte:
`2776ededb323928840a6a0751779dc2927f6a38657391bac1fb9119f24f99e1d`.
Original normal/fallback payloads, shared GRUB binary, firmware and credentials
were preserved. The running diagnostic kernel still has screen_only=Y,
trial_pid=0, pm_test=none and pm_debug_messages=0. Its private trace instance
was removed and private tracefs unmounted. No automatic sleep loop exists.

The experiment was selected through the original default entry only once:
GRUB consumed and reread an environment block before chaining. Real hardware
confirmed the environment changed 1→0. Actual device GRUB was also exercised
in UEFI QEMU: original/modified/restored menus select normal/trial/normal;
a second candidate boot selects normal. No shared firmware was replaced.
This guarantees next-boot selection fallback, **not** recovery from a hard
hang without a reset. The operator was told that limit before reboot.

The diagnostic Image remains in `/media/esp/EFI/SQ1PmTrial/Image` for explicit
reuse; it is not the normal kernel. Normal launcher remains `sq2.el2=1`.
Do not blindly rearm an old script or copy older SQ1 trial images onto SQ2.

## Source and verification

The candidate changes only system-suspend admission in `pm_suspend` and
`enter_state`, adding explicit boot-only debug opt-in and an expiring,
same-task, one-use trial_pid. It reuses the archived 01/04 gate mechanisms
without the old device-prefix bisection. Normal boots still reject sleep.
No SSAM wake-policy source change was deployed.

The isolated kernel source was copied from the available SQ1 EL2 source cache,
then the fixed KSU commit `9b08e88862000d5c50fb2e43a5b75123cf472e54` and its
existing 6.3 adaptation were applied. The kernel configuration is byte-identical
to the current packaged KSU configuration. This is reconstructed-source
provenance, not a claim of reproducing the original Image hash.

Candidate Image SHA:
`1d146837b04032025c508303c9d918fb3144d98e21296495d3f5d7a252d7400b`.
391 effective packaged modules / 18539 undefined references were checked
against candidate kernel plus module exports, with no missing symbols.
This is **not** a complete ABI/modpost proof; complete Module.symvers was absent.
Real QEMU boots checked normal protection and a synthetic failing PM callback
prevented the real-trial gate test from reaching idle. No synthetic module was
loaded on the board.

Source predicate test, identical input:
BASELINE `first=0 second=0 guard=1`;
MODIFIED `first=1 second=0 guard=1`;
ROLLBACK `first=0 second=0 guard=1`, all exit 0.
Eleven additional candidate edge cases passed. Restored hash matched pristine
source and the original source cache stayed unchanged. This is source evidence,
not three physical board boots.

Rebuild recipe: `scripts/build-sq1-pm-trial.sh` (fresh directories only).
The relocated KSU include/uapi symlink must be dereferenced. The first build
failed on that missing header; the corrected build succeeded. The recipe was
syntax-checked; the equivalent recorded work-directory commands built this
Image. Normal standard-build code was not redirected to this candidate.

## Private evidence and next action

The same four source roles and supplemental boot/menu/device records live under
`out/suspend-fix-5.94-20261103/`, ignored by Git. Original role paths are:
- `roles/MODIFIED_FILE.c`
- `roles/DIFF_FILE.patch`
- `roles/VERIFICATION.txt`
- `roles/ROLLBACK.sh`

The rollback script restores a supplied source copy; its optional `menu`
argument restores a supplied menu copy. Do not mistake offline rollback for
a physical kernel rollback. The actual original boot menu was separately
restored and reread on the device. `physical-logs.tar.gz` already contains
the durable device logs; unlike the October checkpoint they have been retrieved.

Next: record/filter the SSAM event ending s2idle, coordinate a physical-key wake
inside the actual machine-suspend interval, then measure residency/power and
repeat recovery checks. Preserve screen-only protection for unattended Android.
Do not claim true low-power suspend repaired, promote to normal, disable SSAM
wake globally, or infer the October black-screen root cause from this one return.
