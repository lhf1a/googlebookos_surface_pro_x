## 2026-11-04：接回81c5c67的本机LTE自动采集构建链

用户明确要求按81c5c673bb695a71a9017c2cf1bf6bd6ddf457aa修复标准通用构建。
此项覆盖下方“保留旧LTE兼容/FSG打包路径”的合并决策：默认EFI router在EL2前采集
本机FSG并使用本机UUID缓存，RAM seed核验来源/CRC后提供RMTFS，Radio为唯一bearer owner。
构建不读取或嵌入modem_fsg.bin、unit.json、单机APN/serial；使用源码AOSP APN表。
恢复上游退休的旧Java NetworkAgent删除与其旧基线测试删除。正常unrestricted和
screen_only=0、nohibernate、共享固件、双机档位、Mesa回退保持，不恢复must-Y门禁。
UUID只是动态缓存归属，不是样机白名单；实际缓存含私有校准，不能分发。
本次只构建、不部署/重启；LTE构建通用化不证明双机新整镜像/长睡稳定性。
标准组包仍使用指定模板和runtime中的原有SSH身份；本地产物不可当匿名公共镜像发布。
U4阶段候选仍是独立打包入口，不宣称标准镜像已自动包含全部U4诊断覆盖层。

## 2026-11-04 retired device-bound diagnostic tools

User requested removal of the old single-machine diagnostics after mainline audit.
Do not restore lte-fs12-seed, lte-fsg-seed, el2-preboot-init or their dedicated
packers/tests from historical merges. Keep normal LTE and U4 paths unchanged.
prepare-sq2-el2-preboot.py remains a shared EFI preparation tool; only its old
RAM-diagnostic packer was retired. Do not bypass NV/calibration or disk safety
checks to revive these tools. Historical documentation is not an active entry.

## 2026-11-04 user-directed mainline integration

On merge conflicts preserve the current unrestricted U4 capabilities and startup
flow. Retain the current LTE/runtime compatibility path, not the newly introduced
EFI LTE router as the default. Incoming touch v7 source changes are retained.
U4 persistent-default ZIP remains private/offline-only, pm_test=devices; do not
silently deploy it or call idle stability repaired. Candidate WSA bootstrap source
is not cold-boot verified. This merge does not authorize device writes or reboot.

# Working agreement

## 2026-11-04 user-directed unrestricted suspend environment

The user explicitly confirmed disabling `suspend.screen_only` and removing every
feature module's must-be-Y dependency. The new default build policy is
`--suspend-policy unrestricted`: boot with `suspend.screen_only=0` and
`suspend.screen_only_autosuspend=0`; no owner registration, trial counter,
admission deadline, diagnostic framework JAR or lab keep-awake hook. This supersedes
historical mandatory-screen-only and owner-only normal-policy requirements below.
Keep the original working Mesa fallback, original payloads and shared firmware.
Do not confuse the real display/screen-on state or KEEP_SCREEN_ON with this gate.
Kernel/module ABI, target-disk validation, firmware integrity, thermal protection,
credentials and nohibernate remain. Android's legitimate wake locks and user
settings remain authoritative. No claim that unrestricted suspend fixes black screen.

## 2026-11-04 user-directed normal power policy

The user explicitly requested normal Android suspend and lockscreen defaults, deployment
on the current device and a push of the development branch. This supersedes historical
screen-only-only/unattended-native prohibitions for the normal entry. Retain screen_only
as a boundary for unregistered callers; the verified Android suspend service gets the
persistent owner gate (01-framework-owner, NOT 02-native-single-request). Keep nohibernate,
matching SPI PM recovery, credentials and the original screen-only Mesa fallback. Remove
lab wakelocks and diagnostic SSAM delays; use stock framework policy. Restore timeouts
only on explicit request, never overwrite user choices on every boot. Do not call single
SQ2 results universal or claim battery efficiency/continuous stability without evidence.

Read docs/HANDOFF.md and docs/BUILD.md before changing the port.

## SPX-PORTABLE-SQ1-SQ2：通用开发与镜像交付（2026-11-03，最高优先级）

- **任意 SQ1/SQ2，不绑定调试样机。** 项目交付目标是同一份标准镜像可在任意 SQ1 或 SQ2 设备上运行，不得要求按每台机器重新编译、改代码或替换设备专属启动包。禁止单机身份绑定；本节覆盖下方历史单机实验约定，完整规范见 `docs/DEVELOPMENT.md`。
- **禁止单机准入。** 不得以某台机器的序列号、NVMe 型号/序列、MAC、IP/SSH 地址、SSH 主机指纹、原盘 UUID/ESP 标记、Android 用户编号或历史采集 SHA 作为通用构建/正常启动的准入条件。日志和实验对象可以记录这些值，但它们不是产品运行条件。
- **按运行时硬件事实适配。** SQ1/SQ2 差异通过型号/SoC、硬件能力与运行时资源发现处理；不得固定一次启动的 phandle、eventN、cardN、节点编号或调试主机路径。硬件类别/版本 quirk 必须有硬件依据，不把某台机器的触摸测量结果推广到所有设备。
- **镜像与本机状态分离。** 通用镜像不得复制调试机的密码/shadow、SSH 私钥/主机身份、Wi-Fi 凭据、IMEI/NV/FSG 校准、用户数据或触摸校准到其他机器。需要的设备唯一数据应由目标设备自己的存储或本机首次配置提供；不得把更换私有文件重新打包当作跨设备启动前提。显式实验室私有包必须单独标记，不能称为通用交付。
- **不撤销安全保护。** 模块/固件 ABI、构建产物完整性、真实硬件兼容性、SMMU/资源边界、screen_only/nohibernate、不可重复启动固件等保护保留。写盘前仍需动态识别本次目标盘/ESP/根源并防止误写；禁止单机白名单不等于允许盲写、清空 userdata 或热重置提供者。
- **跨设备验收，诚实标界。** 同一产物覆盖 SQ1/SQ2、默认/Mesa 两入口以及不同设备身份、容量和枚举资源的回归；旧数据升级不得破坏凭据/用户设置。单台样机通过只证明该次观测，不能声称所有设备已实测。现存单机假设/私有包须列为待整改，不因修改规范就宣称现有镜像已完全通用。

## LTE 执行与收尾约定（2026-11-04）

- 按用户要求跳过离线功能验证/QEMU 前置环节，直接推进已指定真机；一次实验连续完成一组有依赖的改动、采集与观察，减少重启与反复确认。保留原件、可执行恢复、实际访问/内存边界及私有资料保护，不扩展成无意义门禁。
- 复用 SQ2 已完成的 Android LTE 验收。已得到 FSG 地址即采集，不因旧哈希、固定序列/IP/UUID或猜测状态增加 helper 拦截；新的失败做针对性修正。正常运行不携带 LTE 观测器、测试入口、原始协议/寄存器转储或第二个 bearer owner。
- 当前正确路径为本机 EFI GET_INFO/GPT/FSG → 本机 UUID 缓存 → RAM RMTFS → RF/注册 → IPA4.1/RMNET → 原生 Radio AIDL/Android 电话框架。不得将一台设备校准内嵌为通用输入。详见 `docs/LTE-PRODUCT.md` 与 `docs/LTE-EXPERIMENTS.md`。

- Default build/menu (2026-11-02 user instruction): this SQ2 now targets EL2 + Qualcomm Vulkan + native 60 Hz + GPU700/RPMh432. Keep exactly the new default and working Mesa fallback entries; this supersedes the older rescue/shutdown-menu requirement below. Preserve shared GRUB/firmware and both payloads. Normal EL2 uses sq2.el2=1, without experiment recovery timers or continuous diagnostics. Do not restore sq2.el2=experimental in the normal launcher. See the latest HANDOFF for actual reboot results.

- Wi-Fi debugging lesson (2026-11-01): test the full Android candidate early once storage, the screen-only guard and recovery are verified. A failure in a stripped diagnostic initramfs does not establish an EL2 hardware limitation. Compare against the known-working full boot before adding EFI/SCM/SMEM mechanisms; see the debugging retrospective in docs/SQ1-EL2-PREPARATION.md.
- Before each hardware experiment, record one hypothesis, the changed variable, the observation that distinguishes outcomes, and the stop condition. After two consecutive experiments fail to add distinguishing evidence, stop extending that route and return to the working baseline/environment comparison. This is a change of debugging direction, not a requirement to ask the user for permission.
- Verify effective packaged/runtime files, firmware, DT, module dependencies/load order and original error payloads before rewriting a subsystem. Separate board Wi-Fi from guest forwarding; preserve fixes already proven in full Android. Functionality passing does not prove the historical failure's root cause.
- EL2 audio (2026-11-02): the adopted ADSP reports `attached`, not `running`. Accept both active states for the identified ADSP; never reset it to change the state string. Full EL2 boot checks must include the physical ALSA card, AUDIO_READY and actual HAL hash, not only Android boot completion/video/Terminal readiness.
- EL2 stop-pop (2026-11-02): use `patch-sq2-audio-sequencing.py --machine-only`. Trace showed prepare resetting SoundWire while PA was enabled; the SDM845-only patch mutes both PAs before reset and the existing prepare path unmutes after enable. User confirmed sound, no stop-pop, 10-second reopen and the original app. Keep the original WSA881x driver; the combined PRE_PMD version silenced playback and remains excluded. Pin the accepted module pair; HAL/helper/PCM success alone is not listening proof. Extended acoustic testing remains pending.
- Normal EL2 must not open the Terminal UI or run the KVM witness at boot (2026-11-02 user instruction). Keep the explicit passwordless-user login and required AVF/network preparation; the autostart helper exits after login on sq2.el2=1. KVM witness is limited to explicit experimental/debug boot. Remove temporary audio tracing/probes after testing.

- Terminal autologin (2026-11-01): user 10 was verified passwordless (is-user-secure=false, get-disabled=true). The explicitly selected EL2 lab hook now starts/switches this existing user and opens its existing image; a full unattended reboot reached RUNNING_UNLOCKED and Debian shell/HTTPS. Check user/security state before asking for manual unlock. Never infer a credential problem from active user 0; secure users are refused by this helper. No credentials were cleared.

- Shared storage was repaired by loading FUSE before Android init. Both emulated volumes mount and the user confirmed Files and Chrome downloads work. Preserve this fix; never format userdata to repair a mount failure.
- Suspend research was explicitly resumed on 2026-11-03 on `codex/suspend-fix-5.94-20261103`. Preserve normal screen_only/nohibernate and both original payloads. Debug stages and any coordinated single-task trial use isolated opt-in launchers; never globally disable the guard or turn on unattended native autosuspend. The prior forced-wakelock control black-screen failure remains unresolved; see docs/SURFACE-SUSPEND-20261103.md.
- SPI-HID PM regression (2026-11-03): retain the existing SPI sleep patch in EL2 kernel builds. Check actual module/build-id, resume_count and suspend/resume callbacks; prepare/complete alone proves no device sleep. One isolated SQ2 trial recovered by PMIC after temporary per-request SSAM wake exclusion; this does not authorize globally disabling SSAM or promoting native autosuspend. See the latest suspend record.
- Audio work was resumed by the user on 2026-10-27: investigate stop-pop, low/thin/compressed sound and stereo image. Keep gain changes bounded, preserve matching DAC/COMP/BOOST SoundWire ports, and distinguish listening results from register/PCM checks.
- Microphone on 2026-10-28: preserve DMIC0/1 -> MultiMedia2 (CARD_0_DEV_1), raw DEC gain 96, stereo 48 kHz policy and HAL 774cd7e7… with only its built-in microphone address overwrite removed. Stock playback/capture transport is retained; do not reapply the old pacing/MonoPipe experiment. Actual HAL inode/hash and recording/playback were verified after reboot; user confirmed both AudioRecord formats and original diagnostics. Sensor/camera work is deferred at user request; current P1 is Wild Life misrendering despite completed scores, see docs/TODO.md.
- Preserve mapper import/multiplane graphics and touch state fixes. Default contact detection/stability plus AspectMax=2.0 were validated; do not reintroduce high detection thresholds.
- Wild Life compatibility on 2026-10-28: preserve the Turnip sysmem workaround scoped to chip 0x06080001 and com.futuremark.dmandroid.application. Fixed sysmem was user-confirmed normal; scoped patch passed reboot and a full Wild Life run with normal sampled scenes (5264 / 31.52 FPS), then the identical image was promoted to default. This is not a GMEM root-cause fix or proof for other games. Keep other apps' normal heuristics; see docs/WILD-LIFE.md. Respect the user's latest 30-minute screen-off/lock-delay preference and retain credentials.
- GPU update on 2026-10-28: normal boot uses the tested 661 MHz DTB, original 514 MHz DTB remains for rescue. Preserve the matching early-loaded TSENS module and GPU-top 75C passive cooling / 5C hysteresis. Added frequencies use the existing TURBO_L1 firmware corner, not a verified OEM mV curve. SQ2's marketed Adreno 690 still uses SC8180X/A680.1; do not substitute SC8280XP identity/firmware.
- CPU adaptation on 2026-10-28: normal boot now loads the original qcom-cpufreq-hw module and adds missing hardware-LUT-backed 3072/3148.8 MHz big-core OPPs. Preserve schedutil, hardware-provided voltages, cluster passive cooling at 80C / 5C hysteresis, and rescue/sq2.cpufreq=0 opt-out. Reboot and eight-core 20-second checked load passed at 1.7664/3.1488 GHz reported frequency; sampled CPU peak 53.3C. Both thermal maps passed emulation; user confirmed desktop/touch/video normal. Long-term stability and battery testing remain pending.
- Resource monitor on 2026-10-28: preserve resource-monitor-start, SQ2 Thermal HAL sensor mapping and sq2_battery_compat.ko. The module adds read-only energy-to-charge/current/SOH attributes to BAT1 without replacing the Surface battery driver or charging control. Capacity in mAh is estimated at design voltage; 75% SOH is full/design energy, not a fabricated GOOD fault-health status. No battery temperature is available; never substitute CPU temperature. Real charger unplug/replug and reboot passed, user confirmed UI. sq2.monitor=0 skips the adapter; kernel CPU/GPU cooling remains authoritative.
- SQ2 edge ghost taps were reproduced in operator-confirmed idle captures. Preserve the 045E:0C21 / 46x68 heatmap quirk: interpolate only four observed near-saturated edge spikes from their original neighboring cells before contact fitting. Keep normal thresholds and immediate onset/release; the two-frame onset experiment was insufficient and was removed. After a normal reboot, the binary hash matched, a user-confirmed idle 30-second capture had zero touches, and the user confirmed normal corner taps/drag/pinch with no automatic clicks. Extended observation remains pending.
- Keep the normal audio and explicit rescue entries plus shutdown. The user removed the old no-audio second entry. Normal boot must not run diagnostic Wi-Fi association/DHCP, rollback or continuous logging; Android owns saved Wi-Fi state. The user subsequently requested automatic public-key SSH: start it asynchronously after Android boot via ssh-framework-boot (sq2.ssh=0 opts out), without enabling diagnostic networking. Rescue or sq2.debug=1 retains full diagnostics. Audit concatenated initramfs in last-entry-wins order; back up, validate gzip/hash, sync and replace atomically.
- Identify the physical disk, ESP marker, UUIDs and root source before disk operations. Old USB scripts deliberately reject NVMe; do not weaken guards to reuse them blindly. Never touch the workstation system disk.
- Do not routinely unload active QCA Bluetooth transport modules: this previously hung the board. Use the documented HAL/bridge path.
- Initialize Bluetooth transport only after Android boot completion; preserve BluetoothManager's persisted bluetooth_on choice. Do not force enable on boot or add a second state file that can override airplane/user policy.
- Distinguish framework restarts (system_server/SurfaceFlinger PIDs) from board reboots (boot ID). A user long-press reboot is not an autonomous crash.
- PM debug-stage completion does not prove actual suspend works. A framework wake lock does not reliably prevent sleep on this port.
- Keep private/, out/, downloads/, upstream/, work/, keys, credentials and raw logs out of Git. Use the operator's own SSH identity and host verification.
- The user subsequently enabled root password login on the current lab SSH server. Preserve public-key access too. The password and shadow entry stay private; local password-ssh addon must remain last when repacking this device. Standard public builds still default to key-only.
- Report observations and limits honestly; do not label untested patches as working features.
- Root/permissive/debug builds are authorized in this lab. This does not authorize erasing an unidentified disk or publishing secrets.

- 2026-11-04 用户要求：后续任务不要擅自新增按次数、持续时间或异常区域大小拒绝处理的产品门槛。触摸检测已发现异常时，用现有证据生成可撤销候选并进入复测；不能把生成候选写成修复成功。保留设备身份、数据解析、回滚与明确保存机制，不虚构未采到的数据。
