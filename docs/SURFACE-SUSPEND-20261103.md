> 后续：用户已于同日重新接续研发。最新受保护阶段结果和待协调试验状态见文末；历史黑屏失败及其证据缺口仍有效。

# SQ1/SQ2 通用休眠实验收尾 — 2026-11-03

分支：`codex/suspend-fix-5.94-20261103`。工作区：
`/mnt/spx-workspace/chromebookOS`。
本记录接续 [SQ1 初始检查点](SQ1-SUSPEND-EL2-20261103.md)，覆盖随后
SQ1 电源键诊断和切换到 .91 的 SQ2 原生自动休眠实验。

**结论：完成本轮实验与恢复，不是完成休眠修复。**
原生物理按键测试只证明关屏/亮屏、解锁和触摸正常；显式睡眠控制测试
黑屏、短按无反应，需要用户长按关机再开机。未推广为正常默认，
没有继续重复该危险控制测试。本次提交收尾只进行离线检查，不连接设备、
重启、再次休眠或更改默认。休眠目标保持未完成/暂停。

## 通用实现和边界

用户要求实现适用于任意 SQ1/SQ2，禁止按特定机型身份绑定。本次新增的
`surface-pm` 实现不按机型字符串、序列号、IP、磁盘 UUID 或某台设备的
固定输入 SHA 放行；输入清单由实际提供的文件生成。部署时的磁盘身份、
菜单快照和哈希仍用于防写错盘/检测输入变化，不是功能准入条件。
历史 `sq1_` 内核符号及旧诊断文件名不是机型选择分支；历史
`prepare-sq1-pm-once.py` 只复现原固定菜单实验，新部署使用通用
`prepare-surface-pm-once.py`。**实现无单机绑定不等于所有 SQ1/SQ2 已通过实测。**

| 文件 | 具体作用 / 限制 |
| --- | --- |
| `patches/surface-pm/01-framework-owner.patch` | 在已有诊断 gate 基础上修改 `pm_suspend` / `enter_state`；显式 `suspend.screen_only_autosuspend=1` 才允许注册的原生 Android suspend TGID 请求 s2idle |
| `scripts/build-surface-pm-kernel.py` | 接收 source/config，复制到全新 work/output 构建；不会自动替换正常 Image |
| `probe/test2/framework-suspend-start` | boot_completed 后校验实际可执行文件、UID1000、输入生成的哈希，注册 `autosuspend_tgid`；内核持有 TGID 引用避免 PID 复用继承权限 |
| `scripts/patch-surface-desktop-screenoff.py` | 精确修改 `DesktopPowerService$screenStateReceiver$1::onReceive -> WakeLock.acquire(J)` 的 AC 关屏宽限：1585000→1000 ms；不全局禁用应用锁，不改变用户凭据 |
| `probe/test2/framework-suspend-stage` | 仅显式实验启动绑定候选 JAR；post-fs restorecon，保持 SELinux 标签；不替换 userdata |
| `scripts/pack-surface-autosuspend.py` | 接收当前 initramfs/JAR/原生 suspend 程序/实验 lab helper；追加增量并审计 last-entry-wins，私有密码 SSH 层仍最后 |
| `scripts/prepare-surface-pm-once.py` | 保留输入菜单的两项、默认及原 fallback；消费并回读环境成功后才 chain 实验入口，下一启动回原项 |

正常 `suspend.screen_only=1` / `nohibernate` 不变；候选的 shell 或未注册
调用者仍拒绝真实睡眠。实验的 lab helper 在 native opt-in 时跳过人为保活，
其他启动维持原行为。代码未接入标准构建默认，也未加入强制全局禁锁策略。
一次性菜单只保障**复位后的选择回退**，不保证挂死时能自动复位。

## 实验过程：假设、变量、区分证据和停止条件

### A. SQ1 协调按键与 SSAM 事件

- 假设：先前的 kernel wait 被 SSAM 事件提前结束，并非电源键唤醒。
- 变量：保留 SSAM wake，过滤 timekeeping trace 洪泛，协调物理按键并捕获 SSAM。
- 区分观察：`machine_suspend` 1405.962008→1414.695013，8.733005 s；
  wake IRQ173 / SSAM BAT cid0x53（DPTF），而非期望的 PMIC 唤醒。
- 停止条件：一次返回后检查同 boot/UI，不自动循环。用户确认屏幕触摸正常；
  此结果没有满足电源键结束内核睡眠的目标。

### B. SQ1 临时只保留 PMIC 唤醒对照

- 假设：临时排除 SSAM wake 后，物理电源键能结束 kernel wait。
- 变量：单轮临时关闭 SSAM wake；结束恢复，不落地为全局禁用。
- 区分观察：`machine_suspend` 2478.182163→2499.910537，
  **21.728374 s**，PM 日志明确 IRQ216；同 boot，Android 回到 Awake。
- 停止条件：一次试验结束恢复策略。此前一个窗口没有检测到按键，安全退出、
  未请求睡眠；之后延长窗口完成本次观察，不把超时窗口算作睡眠。
- 用户先报屏幕触摸正常，随后报告触摸/上滑动画存在但解锁不了；
  检查用户10为非安全、RUNNING_UNLOCKED，重新唤醒/关闭 keyguard 后用户
  确认“重新点亮后可以了”。未清除凭据。不能据初次反馈宣称完整 UI 恢复无缺陷。
- 这仍是诊断单任务请求，不是原生 Android 电源键自动休眠，也没有能耗测量。

### C. SQ2 (.91) 原生物理按键

- 假设：只移除实验保活和过长 AC grace，Android 原生 suspend 服务即可随
  电源键入睡，并由第二次按键恢复。
- 变量：独立 native-owner Image + JAR/helper 增量；不手写 power/state，
  不覆盖应用 wakelock。先完成一次性完整 Android 启动验证，再观察按键。
- 区分观察：用户实际按键，sleep uptime416057 ms、wake455974 ms；
  **native suspend attempts=0、kernel success=0/fail=0**；
  14378/14378 条 trace 中没有 machine_suspend。应用/JobScheduler 等 partial
  wakelock 仍存在。用户确认正常解锁和触摸。
- 停止条件：两次按键后窗口结束；未再自动请求睡眠。
- 结论：去掉 lab lock/26分钟 AC grace 仍不足以实现本轮自然休眠。
  这次只是关屏恢复通过，不是休眠通过。

### D. SQ2 显式 framework 睡眠控制（失败）

- 假设：使用系统已有的显式睡眠策略暂时抑制普通 partial wakelock，
  能区分“未发起 native suspend”和进入后失败。
- 变量：单次 `cmd power sleep --disable-wakelocks --group-id 0`；
  原计划唤醒后用 `cmd power wakeup --restore-wakelocks --group-id 0` 清理。
- 区分观察：持久日志仅保留假设、候选 boot、框架 PID、基线按键计数及
  `COMMAND_EXPLICIT_SLEEP`。命令输出为空，没有返回状态；
  SSH 超时退出255，用户确认“屏幕全黑，短按无反应”。
- 停止条件：首个失败停止，不重复；用户长按关机再开机，回原默认项。
- 证据缺口：显式控制试验的 trace / 事后 klog 未持久化，pstore 为空；
  清理 trap 未观察到执行。不能判断精确进入阶段、哪个驱动卡住、
  IRQ 丢失、resume 失败或立即重睡，更不能把失联直接当成成功入睡。
  该命令是失败实验记录，**不是推荐修复或复测命令**。

## 完整启动和恢复证据

候选 boot 前缀 `302c66cc`，原生 owner2712，JAR 哈希/标签与注册日志符合。
完整 Android 启动、FUSE 两用户存储、原触摸守护进程、物理 ALSA 卡和
AUDIO_READY 已核对。音频运行 inode 的 SHA 为
`774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`，
不是只检查磁盘上的 HAL 文件；无新增听感验收。保留原音频/触摸/LTE/凭据，
294 项被保护的有效条目一致。

用户手动长按恢复后 boot 前缀 `ffad6382`，原始内核 #14，
boot_completed=1，screen_only=Y，原 normal initramfs；
无 autosuspend 参数，`mGlobalForceDisableWakelocks=false`、
`mGroupsToForceDisableWakelocks={}`，Awake / screen ON / keyguard showing=false。
用户确认回到 Android 桌面。**这是用户手动重启，不是自主崩溃重启。**

| .91 产物 | SHA256 |
| --- | --- |
| 恢复的原菜单 | `adc0afab1f569580388cee950ebd448eab08e177fc2a66ca42a51b381f1d13e2` |
| 保留的原 Image | `c1ef559ff8d240de4f9314ba9d1a2586e7dac3b8eae777bff2a90ed72dd57a1c` |
| 保留的当前原 initramfs | `123aa5b5c799933dbf51c066f534b5e6478d338c347267060f8d05599b8e9ef4` |
| 独立 native-owner Image | `86d896ad89cf535f2c5fe9c106eeb4551893a690f7e35d9e2869e33d0769e810` |
| .91 独立实验 initramfs | `39f1898b3db27d40783d99fe117a561cdf31f0372e8f9da1c12bef9193a82c19` |
| 取回的失败轮持久日志压缩包 | `66b529079191dfb6ad691a393a4cc9b72a5e37064e88ca1275f8c531c79736ac` |

原默认和 Mesa 回退、共享 GRUB/固件保持；独立
`/media/esp/EFI/SurfacePmAuto` 未推广为正常入口。哈希仅记录本次实验输入，
不是通用实现的固定白名单。

## 离线验证、回滚和复现边界

本次收尾重新运行两组菜单单元测试（3+2项）、7个 Python 文件编译检查、
2个 helper/回滚脚本 shell 语法检查，均退出0。相同输入的源码谓词结果：

| 检查 | BASELINE | MODIFIED | ROLLBACK |
| --- | --- | --- | --- |
| 单次诊断 gate | first=0 second=0 guard=1 | first=1 second=0 guard=1 | first=0 second=0 guard=1 |
| native owner gate | owner_first=0 owner_second=0 other=0 | owner_first=1 owner_second=1 other=0 | owner_first=0 owner_second=0 other=0 |
| JAR AC grace | 1585000 ms | 1000 ms | 1585000 ms |

均退出0；候选额外11+4边界用例通过。baseline harness 有未使用变量/函数警告，
原 stderr 完整保留，非隐藏警告。ROLLBACK.sh 在独立副本执行，恢复原 SHA，
再用完整 diff 重建候选并比对哈希。JAR 原件未改，重做 patch 得到相同候选，
两个不同输入 SHA 的 JAR 注释变体均通过并保留注释。

既有实际 Image QEMU 验证普通 guard、单次 gate、原生 owner gate；
以合成失败 PM callback 阻止进入 idle，**不冒充实际硬件睡眠**。
.91 实际 GRUB 在 QEMU 首次选 trial、二次选 normal，环境1→0。
391模块/18539未定义引用的符号存在性审计通过；缺完整 Module.symvers，
不构成全部 ABI 证明。通用构建 recipe 做过语法检查，实际 Image 来自等价的
隔离工作目录命令；未声称此收尾重新全量编译内核。

重现准备需当前端口内核源和配置（含已有
`patches/suspend-el2-trial/01-debug-single-owner.patch`），再由
`build-surface-pm-kernel.py --kernel-source SOURCE --config CONFIG --work FRESH_WORK
--output FRESH_OUTPUT` 构建。JAR 修补接受实际基线 JAR；
打包器需要实际 baseline/framework-baseline/system-suspend 及显式准备的
实验 lab helper，不自动从某台设备猜输入。正常构建不会自动采用这些产物。

离线内核对照曾把原 cpu_suspend 到下一符号的跨度（368字节）与候选函数
大小（316字节）比较；原范围含尾部 linker alternatives，**该大小差异不是
缺失恢复代码的证据**。重定位/alternative 布局也不同，不据此认定根因。

## 证据位置和后续方向

原始日志、设备快照、JAR/内核/凭据均留在忽略的
`out/suspend-fix-5.94-20261103/`，不提交。继续复用四角色：
`roles/MODIFIED_FILE.c`、`roles/DIFF_FILE.patch`、
`roles/VERIFICATION.txt`、`roles/ROLLBACK.sh`。
VERIFICATION 包含命令、输入、原 stdout/stderr/退出状态、哈希、用户反馈和限制。
回滚脚本默认恢复源码副本，`desktop-jar` 模式恢复 JAR 副本；
旧 `menu` 模式是 .94 历史菜单，**不得用于 .91**。
.91 原菜单恢复已在硬件上另行核验，不用离线回滚冒充实机回滚。

若恢复研发，先设计能跨冻结/复位留下的分阶段持久证据，并对照原始完整
Android 与候选的环境/有效文件；不要直接重复强制禁锁或凭空重写驱动。
必须验证真实电源键导致 native suspend、物理唤醒、可解锁/触摸、重复恢复及
驻留/能耗，才能宣称原目标完成。当前没有该证据。


## 重新接续：最新 main 与 SQ2 受保护阶段检查（2026-11-03）

用户明确要求拉取 main、检出休眠开发分支并继续研发。已将本地 main 快进到
`7b189bd`，并合入本分支；合并提交 `7df7710`。原有 MIGRATION.md 未提交内容
完整保留，不纳入本次休眠修改。工作始终在 Ubuntu-24.04 的
`/mnt/spx-workspace/chromebookOS`，Windows 目录只作启动入口。

严格校验已信任 SSH 主机身份后，现场设备确认为 SQ2，恢复后的 boot 仍为
`ffad6382…`；正常 Image、initramfs、菜单哈希与上节恢复记录一致。
旧 ESP 日志已取回，显式禁锁失败轮仍只有 COMMAND_EXPLICIT_SLEEP，无新增
驱动阶段或清理证据；不重写原失败结论。现有内核未启用 PSTORE_CONSOLE/FTRACE，
也没有 ramoops 保留区；空 pstore 不能排除挂死或作为跨复位日志方案。

新增 `prepare-surface-pm-debug.py` 只接收未签名、无 PE checksum 的 ARM64
实验启动器，将唯一 UTF-16 LoadOptions 中的原生 autosuspend 许可改成仅调试阶段
许可。其余字节及文件长度不变，保留原有路径、screen_only/nohibernate。
签名/校验和、重复/矛盾/未知 suspend 选项和缺失保护均拒绝；这不是通用 EFI
编辑器，不修改正常启动器。正常构建没有接入该工具。

新增 `probe/test2/surface-pm-debug` 只接受 freezer/devices/platform，要求
screen_only/test=Y、trial/autosuspend=N、token/owner=0、pm_test=none。
为每次请求建立独立 trace instance，记录 PM 边界和设备回调，过滤
`timekeeping_freeze` 洪泛；请求前落盘基线，返回后保存 trace/klog，恢复原调试
属性、pm_test 和 trace instance。**这是返回后落盘，不能保证挂死后的晚期证据；
超时或一次性菜单均不构成挂死自动恢复。**

独立启动仍复用上节已启动验证的 `86d896ad…` Image 与 `39f1898b…` 实验
initramfs，不将缺失的旧主机缓存伪称为本轮重新编译的内核。仅调试启动 boot
`2fe750ff…`，test=Y、trial/autosuspend=N、token/owner=0；原生框架 JAR 绑定和
owner helper 因无 autosuspend opt-in 退出，保留原 lab lock。原两项菜单在开机后
已恢复原 hash，one-shot env 已实际消费为0。

| 单次请求 | PM 返回 | success | fail | 观察 |
| --- | --- | --- | --- | --- |
| freezer | 0 | 0→1 | 0 | 五秒调试停顿后返回 |
| devices | 0 | 1→2 | 0 | 设备挂起/恢复回调返回 |
| platform | 0 | 2→3 | 0 | trace 含 late/noirq 的进入和返回 |

三项均无 machine_suspend，不能称真实睡眠。boot 保持，用户确认屏幕、触摸、
上滑解锁全部正常。ALSA/AUDIO_READY、attached ADSP、两用户 FUSE，以及实际
音频进程 `/proc/PID/exe` 的 `774cd7e7…` 哈希已核对；没有新增听感验收。

5组启动器/脚本测试（含11种 runner 拒绝条件及清理失败传播）、5项原菜单测试、4项通用规范
测试通过，Python 编译与 shell 语法检查通过。没有把主机拒绝夹具当实机睡眠。
原始包、执行脚本、日志和身份仅在忽略的 `out/suspend-resume-20261103/`。

后续单任务对照使用独立 trial 启动器，仍保留原生 autosuspend 关闭、每任务
五秒一次性 token 和 screen_only。它保留上述同一个 Image/runtime，不使用
`cmd power sleep --disable-wakelocks`。必须等现场按键协调再触发，记录真实
machine_suspend 区间、唤醒 IRQ、同 boot/PID、token 清理及实际解锁/触摸。
当前三阶段通过只缩小调试路径的范围，不解释原生睡眠失败，也不证明能耗。


单任务配置已另行启动到 `5bbbc8a3…`，screen_only/test/trial=Y，
autosuspend=N、owner/token=0；未持 token 的请求在实机被拒绝。
原菜单 `adc0afab…` 已恢复，trial env 已消费0，PMIC 电源键 wakeup=enabled；
该检查时点尚未触发请求；随后用户确认按键协调，单次试验已执行且失败，见下节。
旧脚本不可直接重用：须重新核对 boot、guard、owner/token、IRQ、物理状态与日志目录不存在。
最终 helper 的清理失败传播增量另由主机夹具覆盖；前三项实机检查使用此前
同一控制逻辑版本，不把该小增量说成重新做过完整实机检查。


### SQ2 单任务真实请求：短按仍黑屏（同日接续）

- 假设：先前失败也可能与原生 framework 强制禁锁/恢复控制有关；同一 Image/runtime
  的单任务 token 请求可检验现象是否仅存在于该控制路径。
- 变量：原生 owner 始终0、autosuspend=N；不调用 disable-wakelocks。通过标准
  KEYCODE_SLEEP 仅令框架 Asleep，mGlobalForceDisableWakelocks=false、group 集合空，
  lab partial lock 仍保留；只有发起 sysfs 请求的当前任务获五秒单次 token。
  为容纳两个 opt-in，独立试验启动器删除了 earlycon 和 printk.devkmsg 参数，
  保留 screen_only/nohibernate、EL2、DT、Image、initramfs、图形/音频/网络配置。
- 区分观察：用户确认可以按提示约15秒短按电源键；请求后 SSH 未返回，随后
  keepalive 超时。用户明确反馈“仍黑屏，短按无反应”。没有自动重试。
- 停止条件：首个失败即停止；已指示用户长按关机再开机回原默认入口。
  原菜单在真实请求前已经逐字恢复，one-shot 环境已消费0。

此结果表明单任务路径也出现相同用户可见失败，不能把问题限定为那条原生
强制禁锁命令。它**不**证明进入了 machine_suspend、CPU 低功耗、某个驱动卡死，
也不证明 token 清理完成；须恢复后读取实际文件。尤其三阶段调试时为框架 Awake，
本次真实请求前为 Asleep，设备 runtime PM 状态可能不同；不能用这组比较排除
所有设备回调，更不能直接归因为 EL2/PSCI/CPU idle。下一步应先补同一屏幕状态下
的受保护阶段对照及跨冻结取证，不能直接再做无新证据的真实睡眠。


### 实际回退闭环与保留状态

用户长按关机后开机，并确认回到桌面。SSH 实读新 boot `baa5a989…`，
原内核 #14、正常 cmdline 无 test/trial/autosuspend 参数，screen_only=Y、
Android completed1 / Awake，force-disable=false、group 集合为空。
`esp-before.sha256` 中27项（含原菜单、两个完整载荷、共享 GRUB、DT和固件）
全部校验通过；debug/trial 两份 env 均为0，实验入口未提升默认。
三个 remoteproc 仍 attached，ALSA/AUDIO_READY、两用户 FUSE 和实际音频
进程哈希 `774cd7e7…` 复核通过；不是新增听感验收。

`real-1` 只保存 boot/PID/IRQ/计数基线、cmdline 以及 REAL_REQUEST_START；
没有 PM_RETURN、token-after、boot-after、trace 或事后 klog，pstore 为空。
基线 fail=1 来自本轮先前有意做的未持 token 拒绝检查，不是一次实际睡眠失败。
本轮不能判定 token 写入/消费和 cleanup 的最终状态；复位后旧内核已无该许可。
不把用户长按恢复称自主崩溃或成功唤醒。当前停止真实睡眠试验，保留原正常运行。

后续先对齐 Asleep/Awake 的有效 runtime 环境，再设计验证过的跨冻结证据路径；
不继续重放真实请求、不全局关闭 SSAM、不盲猜 EL2/GIC/PSCI 根因。新增 helper
的副本回滚/完整 diff 重建记录在本轮私有 roles 中，原旧四角色不在已迁移工作区，
没有伪称已恢复旧主机未迁移的原始文件。正常/回退入口实际恢复已另行实读，
不以副本回滚替代设备恢复。


## 按用户提示复查历史逐组件响应：发现 SPI-HID PM 构建回归

历史 `sq1-suspend-wip/02-device-prefix.patch` 与03策略补丁通过关闭 async、
限制 prepare/suspend 列表前缀并主动返回 ECANCELED 逐步定位，曾将 QCA 蓝牙
ETIMEDOUT 隔离出来。计数包含没有实际硬件回调的设备列表节点，不能直接当
独立组件通过数；前缀补丁只允许 TEST_DEVICES，不复制到当前默认或真实睡眠路径。

本轮已有的完整 `device_pm_callback_start/end` 不必重跑即可逐条分析。新增
`analyze-surface-pm-trace.py TRACE FRESH_DIRECTORY`，按任务、驱动和设备配对
（允许跨CPU迁移），结合 dpm 边界区分 prepare/suspend/late/noirq 和恢复阶段，
输出 callbacks.csv、含parent的 components.csv 和 summary.json。
保留错误码、缺失边界、缓冲区覆盖信息；未知回调格式报错。prepare/complete
的0返回不能冒充驱动实际执行了睡眠回调。4组异步/迁移/错误/缺失/格式/新输出测试通过。

原 devices trace 的2286条和platform trace的2334条回调记录均配对返回，err全0；
platform trace为4694/4694条，无记录覆盖。942是driver/device字符串组合数，
不是942个硬件均已休眠。关键样本（本轮Awake调试，毫秒）：

| 回调对象 | 常规 suspend | 常规 resume | 备注 |
| --- | --- | --- | --- |
| QCA UART 蓝牙 | 0.002 | 0.002 | 均返回0，不证明蓝牙控制器低功耗 |
| Wi-Fi phy | 109.076 | 7.667 | 均返回0 |
| NVMe PCI设备 | 182.846 | 0.007 | noirq另12.500/12.349，均返回0 |
| SSAM串口hub | 2.556 | 3.189 | 均返回0 |
| DP显示控制器 | 1.125 | 1.137 | 均返回0 |
| DPU | 0.094 | 0.002 | complete另348.413，返回0 |
| SPI-HID触摸屏 | **未出现** | **未出现** | 只有prepare/complete，不能算休眠回调通过 |

针对 SPI-HID，当前正常运行模块 ELF build-id 与取回文件逐字相同；文件
SHA `31ee23c6…`，实验 initramfs 的有效条目也是相同SHA。运行 sysfs 没有
resume_count，运行 kallsyms/ELF 都无 spi_hid_pm_suspend/resume/ops。
匹配的 EL2 内核源码也未包含旧 `SQ2_SPI_PM_V1`，EL2 builder 未调用既有
`patch-sq2-spi-pm.py`；早期 lid-power builder 则有该调用。这确认旧PM改动未进入
当前EL2模块，不是仅凭trace缺项猜测，**仍不是本轮黑屏根因证明**。

EL2 builder补回这一条既有patch调用；不重写SPI子系统、不热卸当前触摸模块。
在独立模块目录以匹配正常Image `c1ef559f…` 的kernel/build编译，运行配置逐字
等于build .config，Module.symvers存在。原源码/旧PM补丁/回滚三轮模块编译完成；
回滚模块与本次baseline逐字一致。初次本地header未来mtime警告保留，修正副本mtime
后另做三轮最终构建，最终日志无警告，严格modpost通过。
候选strip后SHA `072cdec5…`，67个未定义引用全部匹配运行__ksymtab，vermagic一致。
这不声称全内核重新编译或覆盖每项ABI属性。

独立实验包 `e4210f44…` 只改变一个有效SPI模块，其他条目与最后凭据层不变；
匹配debug-only启动器关闭trial/autosuspend，仅允许受保护调试阶段。当前候选
启动/逐组件回调验证结果另续；正常输入与默认/回退载荷尚未采用此候选。


### SPI-HID 候选逐组件实机验证

独立 debug-only boot `4c9d039f…`，已加载模块文件SHA `072cdec5…` 与候选一致，
运行ELF build-id note也逐字匹配。运行 kallsyms出现两个PM函数/ops，resume_count
从0开始；screen_only/test=Y、trial/autosuspend=N、owner/token=0，原菜单已恢复、
一次性env0。完整Android、物理ALSA/AUDIO_READY、实际HAL774cd7e7…和两用户FUSE通过。

| 单次调试请求 | SPI挂起 | SPI恢复 | resume_count | PM返回/错误 |
| --- | --- | --- | --- | --- |
| Awake devices | 0.191 ms | 24.452 ms | 0→1 | 0 / fail0 |
| Asleep platform | 0.114 ms | 22.150 ms | 1→2 | 0 / fail0 |

关屏用标准KEYCODE_SLEEP，确认框架Asleep且未强制禁锁后运行；返回后用
KEYCODE_WAKEUP回Awake。两轮同boot；touch-resume助手按计数1/2实际重新初始化。
用户确认画面/触摸/上滑解锁都正常。Awake devices的2288条、Asleep platform的2336条
回调记录全部配对返回、err0，均无缓冲覆盖；SPI driver层的suspend/resume已直接记录，
不再仅凭prepare/complete推定。不是machine_suspend或实际能耗验收。

候选未提升默认，源代码builder补回旧PM patch不会自动替换正常私有runtime。
新的一次真实对照已在主机准备，只改变相对前次单任务试验的候选initrd，等待用户
确认是否继续现场按键配合；未重放旧one-shot脚本或自动触发。


### 候选真实对照：SSAM 早醒与一次 PMIC 电源键唤醒成功

用户明确继续一次真实对照，独立启动到 `1df5aab6…`：原Image/DT不变，
只采用已验证SPI模块的实验initrd；原菜单在试验前恢复、env已消费0。
原生owner0/autosuspend=N，screen_only=Y；每次仍需当前任务五秒token。
完整Android、ALSA/AUDIO_READY和实际HAL774cd7e7…已检查。

第一次保留原SSAM wake：请求返回0、SPIcount0→1、token0，但trace的
machine_suspend只有124.525716→124.560607，即34.891ms；日志明确IRQ173
SSAM触发早醒，不能当成约15秒后的电源键唤醒。用户确认UI正常。

随后另获用户明确现场配合确认，**同boot、同代码/载荷**只在这一请求期间
临时将动态发现的surface_serial_hub设备wakeup从enabled改disabled，保留
PMIC电源键enabled。结束恢复原值；不改boot helper、不全局关闭EC唤醒。
触发前记录新假设/唯一变量/区分观察/首个失败停止条件；不自动循环。

| 本轮 PMIC 对照 | 实际观察 |
| --- | --- |
| machine_suspend | 347.650290→363.338477，**15.688187秒** |
| 唤醒原因 | 内核明确 Triggering wakeup from IRQ216 / pm8941_pwrkey |
| PM返回/计数 | 返回0，success1→2，fail0→0 |
| 组件响应 | 2338条回调记录全部返回、err0，无缓冲覆盖 |
| SPI恢复 | resume_count1→2，ready；触摸助手执行重新初始化 |
| 连续性 | boot不变，system_server3820 / SurfaceFlinger3361不变 |
| 清理 | token0，SSAM读回enabled，pm_test=none、debug/times恢复0 |
| 用户实测 | 明确确认画面、触摸、上滑解锁全部正常 |

这是本SQ2一次受控单任务的s2idle等待/电源键唤醒证据，**不是**能耗/CPU低功耗
驻留测量、长期/多轮稳定性、SQ1新回归或原生Android自动休眠完成。
SSAM后续IRQ173在machine_suspend结束后才出现，不能覆盖本轮IRQ216的唤醒原因。
旧未打SPI补丁的真实黑屏仍保留，未故意回装缺陷模块重现黑屏；因此确认SPI PM
构建遗漏和候选行为改善，不把它提升为所有历史黑屏唯一根因。

最终仍运行受保护候选：screen_only/test/trial=Y、native=N、owner/token0，
无新请求或循环；下次启动回原正常。原ESP清单27项全部匹配，默认/Mesa/共享
GRUB及固件保持；ALSA、AUDIO_READY、实际HAL哈希、attached remoteproc和两用户
FUSE复核。自己的trace instance/额外mount层已清理，Android原bootreceiver/wifi
instance及原tracefs挂载保持。没有更新正常私有runtime输入或提升实验入口。

下一步处理SSAM事件过滤/原生唤醒策略，避免把临时排除早醒变成全局禁用；
再做原生自动休眠、重复恢复与能耗，之后才讨论正常集成。逐组件原始CSV与
PMIC结果JSON在 `out/suspend-resume-20261103/spi-pm/real-pmic-responses/` 和
`pmic-result.json`，均忽略，不提交身份日志。


### 接续目标“休眠可用”：SQ2 SSAM 事件定位，尚未完成原生休眠

上一轮用户再次确认15.688187秒PMIC唤醒后的画面、触摸、解锁正常。
接续实读仍为boot1df5aab6…、success2/fail0、nativeN/owner0/token0。
本轮没有重启或再次发起真实睡眠，也未修改正常启动入口或SSAM wake策略。

- 假设：SSAM在display-off/D0-exit后有可识别的EC事件排队；不能把旧SQ1的
  BAT/DPTF cid0x53直接套到这台SQ2。
- 变量：一次Asleep下的受保护platform阶段，增加SSAM事件/请求/响应及动态IRQ追踪。
  trial许可虽在该实验boot存在，本次token始终0，pm_test=platform，不进入machine_suspend。
- 结果：PM_RETURN=0、cleanup=0；4760/4760条trace无覆盖，2340条组件回调全部返回0。
  success2→3/fail0，SPIresume_count2→3，同boot和system_server3820/SurfaceFlinger3361。
- SSAM：1173.094422在late阶段出现IRQ173；D0-entry后的1178.151355起集中释放
  `sid=KIP tc=HID cid=0x00 iid=0x01 len=12`事件。全窗口18条此类事件，另有2条
  iid0x02/len3事件；本轮未捕获BAT事件。挂起前/恢复后部分报告间隔约100ms。
- 实际HID_PHYS/描述符将iid1对应到键盘接口045E:09AE；12字节是Report1的
  1字节ID、8位修饰键和80位键值数组。元数据未包含报告内容，不能据此认定空报告、
  卡键、无人操作或唯一早醒根因；也不能证明之前34.891ms真实试验的事件相同。
- 随后一次有界5秒hidraw只读采样为0字节，没有有效报告可解码，不能称已证明周期性
  空报告。现场是否操作键盘盖以及本次UI恢复反馈仍待用户确认。
- 结束实读Awake、强制禁锁false/组集合空、SSAM和PMIC均enabled、pm_test=none、
  owner/token0、nativeN；仅自己的trace挂载/instance已清除，Android原tracefs保留。

`surface-pm-debug`增加三个可选SSAM tracepoint；缺少该子系统的内核仍可执行原
受保护阶段，拒绝真实睡眠的准入条件未改变。专项测试覆盖无/部分/完整tracepoint。
实机使用独立私有单次脚本适配当前trial启动，原助手仍要求trial=N，未将例外放宽到
公开助手。新脚本和原始证据在忽略的`out/suspend-resume-20261103/ssam-events/`。
下一步先对齐无键盘操作的对照，必要时键盘盖分离定位，再决定EC事件策略；原生自动
休眠、多轮恢复与能耗仍待验证，不能以本轮platform检查宣称目标完成。


### 无操作/键盘盖分离对照与保留 SSAM 的真实电源键恢复

用户说明上一轮曾点击设备，要求重新测试。本轮先做无人操作对照，再只读采集
键盘hidraw报告，随后由用户拔下键盘盖做相同platform对照。每轮独立新日志，
开始前核对同boot、当前IRQ/PMIC、SSAMenabled、nativeN/owner0/token0；首个失败停止。

| 对照 | 回调/trace | SSAM事件与IRQ | PM计数 |
| --- | --- | --- | --- |
| 无操作、键盘盖接入 | 2340返回0；4760/4760 | 18条iid1/len12、2条iid2/len3；IRQ173一次 | success3→4/fail0 |
| 无操作、增加只读hidraw | 2340返回0；4762/4762 | 同样18+2条；IRQ173一次 | success4→5/fail0 |
| 键盘盖拔下 | 2260返回0；4576/4576 | 无SSAM事件、无SSAM IRQ | success5→6/fail0 |

三轮均无machine_suspend、无trace覆盖。只读键盘采集开始后关屏前2秒为0字节，
整个挂起/恢复窗口得到216字节，18条Report1全部为ID=1且11字节输入字段全0，
没有修饰键/按键按下。读者由18秒timeout结束（143），不是读取失败后伪造空报告。
这些空报告发生在EC显示状态转换周围，接收时间并不能完全代表EC生成时间。
用户确认前两轮期间未操作设备、恢复正常。这支持键盘盖电源转换报告排队的方向，
不证明是哪一条通知命令导致，也不是完整EC事件过滤方案。

随后另获用户按键协调确认，键盘盖保持拔下，**SSAM和PMIC始终enabled**。
原ESP27项校验全通过、所有once.env=0，不改菜单/载荷；独立单任务五秒token
只发起一次真实请求，不自动重试，也不调用强制禁锁或自动KEYCODE_WAKEUP。

- machine_suspend：1903.952181→1917.916734，**13.964553秒**。
- 内核明确`Triggering wakeup from IRQ216`，trace为pm8941_pwrkey；没有SSAM事件。
- PM_RETURN/cleanup=0，2260条组件回调全返回0、无覆盖；success6→7/fail0。
- SPIcount6→7、同boot1df5aab6…和system_server3820/SurfaceFlinger3361。
- 用户确认亮屏/触摸/返回桌面正常，随后补充当前没有上滑锁屏，详见下面更正。
- 清理实读pm_test=none、debug/times0、token/owner0、nativeN、SSAMenabled；
  自己的trace挂载/instance已清理，Android原tracefs保持。没有提升默认。

这证明本SQ2在**键盘盖拔下且保留SSAM唤醒**的条件下有一次受控电源键恢复。
不能将“需拔键盘盖”作为最终可用方案；带盖事件策略、原生自动休眠、多轮恢复和
能耗仍未完成。原始证据/解析JSON在`out/suspend-resume-20261103/ssam-events/`。
上游当前controller.c的IRQ处理仍注明EC事件回调/过滤未实现；仅核对源码，未换驱动。

### 用户更正：当前无上滑锁屏，收窄此前UI验收表述

用户指出当前直接进入桌面，没有上滑步骤。实读前台user10的
`locksettings get-disabled --user 10=true`、Keyguard showing=false/inputRestricted=false；
这是锁屏None。11月1日HANDOFF已有get-disabled=true记录，早于本轮休眠试验；
本轮只执行KEYCODE_SLEEP/WAKEUP，没有set-disabled、dismiss-keyguard或凭据修改。
不能据当前状态确定最早是哪次临时处理留下。

**此前本SQ2记录中的“画面/触摸/上滑解锁均正常”须收窄为“亮屏/触摸/返回桌面正常”。**
用户当时“都正常”不构成实际执行上滑或安全凭据解锁的证据。13.96秒与15.69秒的
内核电源键唤醒证据仍有效，锁屏/安全解锁验收未做；保留现有设置，不清除或新增凭据。


### SSAM 关屏报告排队时序：隔离延时诊断候选已准备，尚未启动

继续沿键盘盖对照定位。原trace在display-off通知后约100ms开始接收空键盘报告；
D0-exit后剩余报告集中到恢复阶段释放，普通Awake被动窗口没有报告。
当前待验证假设：在关屏通知与D0-exit之间保留窗口，可能避免将有限的释放报告排队
成wake IRQ。报告的确切生成机制尚未确认，不将固定延时当作最终修复。

从匹配正常内核的SSAM源码复制到独立work目录，仅在成功的pm_prepare/display-off
通知后增加1100ms诊断延时及日志，不改事件订阅、wake开关或其他PM路径。
正常源码/构建入口未接入此变更，不热卸载活跃SSAM。基线/候选/回滚模块严格构建，
Module.symvers存在、baseline与rollback逐字相同；最初时钟偏差告警完整保留，
最终候选在副本mtime归一后无告警且二进制与前次一致。副本回滚及完整diff重建通过。
当前模块file/note和有效cpio匹配，候选148个外部引用全部找到实际内核导出，vermagic匹配。

独立模块97d51f45…，实验initrd a954879e…只改变一个SSAM模块、保留已验证SPI PM与
最后凭据成员；debug-only启动器a37b1278…仅允许受保护PM阶段，trial/native关闭。
实际ESP挂载为动态核对的nvme分区6，原27项完整性清单全通过；候选传到独立路径并
hash/gzip通过。**尚未安装一次性菜单、未重启、未做候选实机PM测试**；等待现场确认
键盘盖接入及启动观察。正常两入口/载荷保持原状。候选与四角色证据仅在忽略的
`out/suspend-resume-20261103/ssam-grace/`，不作为已验收休眠功能。


### SSAM 延时诊断：加载与两次受保护检查完成

用户确认键盘盖已接上并在场观察后，启动到boot6a06cbe5…；加载模块文件SHA
97d51f45…且sysfs ELF note与候选逐字匹配。testY、trial/nativeN、owner/token0；
原菜单立即恢复、once.env消费0，原ESP27项均匹配。ALSA/AUDIO_READY、真实音频
进程774cd7e7…、三个attached remoteproc、两用户FUSE检查通过，用户确认桌面正常。

两次Asleep/platform均返回0、2336条回调全部返回、无trace覆盖，success0→1→2/fail0，
SPIresume_count0→1→2。第一轮有一次PMIC中断，用户确认按过电源键且恢复正常；
因此补做一次不按键对照，第二轮既无SSAM也无PMIC IRQ，用户确认恢复正常。
两轮都没有machine_suspend，不能当真实睡眠。

第一轮关屏通知157.975673；9条iid1键盘报告从158.083762至158.886149，全部早于
D0-exit159.593812。D0-entry164.647683后不再出现积压键盘报告，display-on164.951194
之后另9条报告正常到达。这与原候选一部分报告积压到D0-entry的时序有明确区别，
支持“关屏转换报告需先送完”的方向，但不能只凭这一固定延时推广到其他键盘盖/EC。

已准备同模块的单任务trial启动器92d0ebf9…；用户确认继续真实按键对照后发出
一次性重启，正常载荷保持，native仍关闭。新boot的验证和真实请求结果待下节。
低功耗测量入口已发现：每核cpuidle/state*/s2idle/{time,usage}可读；当前内核未开启
CONFIG_QCOM_STATS。此前只有累计值、没有逐试验差分，不作为硬件驻留通过证据。
AC在线、BAT Full/power_now0，不能用此值声称零耗电；放电能耗仍需独立验证。


### 2026-11-04：32.20秒电源键恢复后触摸失灵，候选不通过，退出实验恢复正常启动

真实单任务boot86ef9211…使用相同SSAM诊断模块97d51f45…，键盘盖接入且SSAM/PMIC
wakeup均enabled。machine_suspend133.037388→165.240756（32.203368秒），内核明确
PMIC IRQ216唤醒，无SSAM早醒；2336条回调返回0且无trace覆盖。用户初报亮屏/触摸/
键盘正常，随后明确报告“触摸失灵了”。**因此本轮不能标为完整恢复通过。**

故障时同boot，SPIresume_count=1，但ready持续为`not ready`；触摸助手记录
`TOUCH_RESUME_NOT_READY count=1`。SPI恢复日志有全零报头/invalid sync constant，
PM回调仍返回0；可见回调返回码不能替代设备ready和持续输入验收。没有据此确认唯一根因。
保存故障日志后仅调用已有touch-framework-ready重新初始化用户态：旧IPTSd停止，
重新打开后等待12秒仍SPI_READY_TIMEOUT、退出3；未热卸载驱动，没有反复重试。

用户已暂停“休眠可用”目标，本次仅处理故障恢复，不继续睡眠试验。核对原ESP27项
全部匹配、7份once.env均为0、owner/token0后，重启回原正常启动包；恢复实测另续。
原始故障快照、重新初始化结果与回退核验存于
`out/suspend-resume-20261103/ssam-grace/touch-failure/`。正常构建未采用SSAM延时或SPI
实验候选。每核s2idle/time虽增加约5.16–7.66秒，与32.20秒窗口不等，不作能耗/驻留达标证据。


回退闭环：新boot `c3898a5d…` 实读正常内核#14、原默认initrad04.cpio.gz、
screen_onlyY/nohibernate，无trial参数；SPI ready、IPTSd运行并重新发布虚拟输入。
原ESP27项重新校验全通过，Android boot_completed1/Awake、强制禁锁false/组集合空。
用户明确确认“已到桌面，触摸恢复”。恢复期间没有发起新的睡眠；目标继续保持用户暂停状态。


### 2026-11-04：恢复研发，SPI-HID 全零报头恢复候选

目标恢复后保持原系统，不重放真实睡眠。源码ll_open在power_up/deassert_reset前启用IRQ，
故障日志中全零报头也早于open完成；无效报头增加计数后直接退出，没有排入既有error_work。
这支持时序/恢复缺口假设，尚不能证明是唯一硬件根因。候选将IRQ移至电源/复位稳定后，
并在无效报头分支安排已有有界错误恢复；不增加重试循环、不阻塞等待ready、不改校准。

`scripts/check-spi-hid-resume.py`抽取真实open/header函数编译执行，ASan/UBSan验证旧行为
与修复后的时序、供电/复位失败、零报头/版本错误/超长、正常数据/响应、PM/off保护。
fresh/V1升级结果一致、幂等、未知锚点拒绝且不部分写入通过。匹配完整Module.symvers
严格构建baseline/modified/rollback，基线与回滚二进制相同；初始Makefile时钟偏差记录保留，
校正独立副本mtime后最终三轮零告警且产物一致。完整diff重建/副本回滚通过。
实际IRQ为msmgpio122 Edge；匹配pinctrl-msm保留masked边沿锁存，unmask不ack；仍需实机
验证恢复边沿。67个外部符号匹配新取的运行内核导出，vermagic一致。

私有候选模块3e9e6317…、包2e4427e4…、debug启动器f75c4ecb…，只替换上一SSAM诊断包
的SPI模块、保留最后凭据成员。SSAM固定延时仍仅为诊断，不写入正常builder。
原ESP27项校验全通过；候选已写独立路径并通过hash/gzip，正常载荷保持。
用户确认键盘盖接入并在场，下一步一次性启动后进行受保护检查。

本轮实验假设：稳定后接收中断并补齐无效报头恢复，可避免SPI恢复后长期not ready。
变量：仅SPI模块两个紧密相关输入恢复修正；SSAM诊断、内核和其他载荷保持上轮。
区分观察：实际加载note/hash、SPI ready和错误计数、回调/IRQ时序、持续触摸输入。
停止条件：首个ready超时、输入失败或异常即取证回退；不自动重试真实休眠、不热卸载。
证据位于忽略的`out/suspend-resume-20261104/spi-input/`。实机结果尚未取得。


候选debug启动boot3daff131…成功，用户确认桌面、触摸和键盘正常；实读SPI文件
3e9e6317…且loaded ELF note与候选一致，SSAM保持97d51f45…。恢复原菜单后ESP27项全匹配，
8份once.env均为0。音频HAL774cd7e7…、AUDIO_READY、ALSA/FUSE和attached remoteproc检查通过。

一次Asleep/platform受保护检查返回0，2336条回调全返回0、无trace丢失；SPIcount0→1，
ready恢复，IPTSd换新进程，Android框架PID保持。关键日志：open结束156.638036后，
156.638579仍有全零报头，156.638682随即Error Handler，最终ready且bus_error1→2。
因此“仅推迟IRQ即可消除全零报头”未获支持，但新错误恢复路径本轮确实被触发且恢复；
不能将两处改动共同成功当作唯一根因证明。用户连续操作触摸约半分钟后确认持续正常，
键盘也正常。尚无本候选真实machine_suspend；原上轮真实恢复失败结论保留。


真实候选bootb3221fcd…：用户确认可继续后一次性启动，SPI loaded note/hash匹配，
原菜单恢复、ESP27项与9份已消费once.env核对通过。单任务请求仅一次，nativeN，
SSAM/PMIC唤醒保持enabled，没有自动KEYCODE_WAKEUP、没有强制禁锁或重试。

本轮machine_suspend138.468802→269.867014，即131.398212秒，唤醒IRQ173/SSAM；
首个恢复事件为SAM/BAT cid0x53/iid1/len21，随后有键盘盖报告。本轮没有PMIC中断，
**不能称电源键唤醒通过，也尚不能判断用户是否按过或输入是否恢复。** 网络恢复后实读
Android仍Asleep，等待用户对实际按键/画面/持续触摸的反馈；未用软件亮屏掩盖这个结果。

PM_RETURN/cleanup/wrapper均0，success0→1/fail0，2336条回调全返回0且无trace丢失，
框架PID保持。SPIcount0→1，open结束168.427240后168.427919出现全零报头，
168.428021进入Error Handler；最终ready，bus_error1→2，IPTSd重新初始化并保持运行。
随后读回仍ready且错误计数2。该证据支持真实resume路径的驱动就绪恢复，但没有用户
实际输入确认，不能替代完整恢复验收。s2idle统计约21.85–29.85秒，不当131秒整段硬件
驻留或能耗达标证明。

临时trace挂载已清理，pm_test none/debug0/times0，owner/token0、nativeN、SSAMenabled；
原ESP27项仍全部匹配，下一次普通启动为原正常入口。源码与受保护阶段记录已提交7adbed3，
未推送。当前保持候选但不再发睡眠请求，等待现场反馈。私有证据见spi-input/real-1与real-responses。


继续目标时只读核对：仍为bootb3221fcd…，SPIready/count1/bus_error2，SSAM IRQ计数1、
PMIC电源键IRQ计数0，Android仍Asleep、nativeN/owner0/token0。尚无现场按键/输入反馈，
没有重试休眠或自动软件亮屏。

为后续原生路径取回`spi-input/native-blockers.txt`：AC在线，Wake Locks共9项，
包括实验PowerManagerShellCommand、DesktopPowerService:ScreenOff及充电后台作业/
维护同步锁；mHoldingWakeLockSuspendBlocker=true、DisplaySuspendBlocker=false、
native suspend attempts=0。当前trial本来就不启用native/JAR修正，因此这不是新native
候选失败证据，也不能把合法后台作业一律认定为bug。后续须在物理恢复验收闭环后，以
独立native入口只撤销实验保活/修正桌面宽限，保留应用锁，在适当空闲/电池条件观测。
不靠全局禁锁或强停用户任务使计数增加。

当前SSAM源码明确未实现GPIO callback逐事件释放/选择性唤醒：任何IRQ都能结束
s2idle，电池事件也不例外。本轮首事件BAT与这个限制一致；1.1秒诊断延时只避免了
关屏转换期间的键盘报告排队，并不构成完整EC唤醒策略。不在现场反馈缺失时盲加
事件过滤或更改活跃UART/SSAM，下一步首先闭环真实物理电源键与持续触摸。


现场反馈补齐：用户确认“已亮屏，触摸持续正常，键盘正常”。随即只读核对仍是
bootb3221fcd…，SPIready/count1/bus_error2，IPTSd与Android框架PID保持；PMIC IRQ
从0增加到2，Android Awake，mLastWakeReason=1（电源键）。mLastWakeTime1355670ms，
明显晚于本轮machine_suspend结束269.867014s；因此这是**SSAM先结束内核睡眠后，
电源键唤醒Android界面，持续输入正常**，不是PMIC直接结束内核睡眠。

阻塞的现场输入证据已补齐；本候选真实resume的触摸恢复获得本轮用户验证。
仍缺同候选由PMIC直接唤醒，以及重复恢复/原生策略/能耗验证。准备同boot第二次
独立单任务对照：只改变按键协调时间，保留SSAMenabled、相同模块和保护；新日志目录
及SPIcount1→2检查防止重复执行。尚未触发，待现场配合确认，不自动重试。


### 2026-11-04：同候选第二轮真实休眠由PMIC结束，持续触摸/键盘通过

用户确认可配合后，在同bootb3221fcd…、相同模块与wake策略下，仅运行新日志目录的一次
任务请求。machine_suspend3380.674249→3432.275442，51.601193秒；内核明确IRQ216唤醒，
trace记录pm8941_pwrkey按下/释放，无SSAM唤醒IRQ。2336条回调全返回0且无丢失，
SPIcount1→2、bus_error2→3，恢复全零报头后Error Handler完成恢复，ready/IPTSd正常。
PM_RETURN/cleanup/wrapper0，Android Awake，全局强制禁锁false/集合空；用户明确确认
已亮屏、触摸持续正常、键盘正常。这是本SPI候选的直接PMIC唤醒证据，与前轮SSAM唤醒
后再按键亮屏区别记录。两轮真实恢复通过仍不代表原生自动休眠或能耗已验收。

末态：nativeN/owner0/token0、pm_test none/debug0/times0、SSAMenabled，自己的tracefs
已卸载；原ESP27项匹配、9份once.env=0，正常载荷与默认菜单未变。新日志在
spi-input/real-2、real2-responses与real2-final-state.txt。接下来离线准备受限的原生
服务试验：人工注册owner前保持guard，只准入一次请求并可撤销，保留应用wakelock；
不把当前诊断1.1秒SSAM延时提升正常，也不直接启用无人值守autosuspend。


### 原生服务受限试验：离线候选与检查

准备02-native-single-request诊断补丁；正确的注册TGID在120秒boottime窗口内仅有一次
s2idle准入，消费后owner引用与数值清零。它用于下一步原生路径验证，不是正常自动休眠
实现。人工注册前保持owner0；候选init删除自动注册调用，既有实验JAR/manifest与lab
native分支保持，SPI/SSAM及其他391模块完全不变，不触碰用户锁/凭据。

在独立work/native-once-20261104复制匹配源码/构建树，应用既有debug/owner两补丁后
再应用新补丁，配置逐字一致。实际谓词ASan/UBSan、正向patch重建/逆向回滚通过。
初次构建clock-skew输出保留，最终增量构建无clock-skew且Image字节一致；既有KSU
非git版本fallback告警仍存在，不宣称全零告警。QEMU最初缺lsm=selinux导致KSU启动
空指针，与休眠无关；保留失败日志，对齐实机参数后同Image通过normal guard和native
单次注册测试（UID1000首次到达合成-EAGAIN回调、第二次-EOPNOTSUPP、owner0）。
这是实际内核准入测试，非真实硬件睡眠。检查全部391个模块的18541项外部引用，均在
新vmlinux或原包依赖模块导出中找到；只证明符号存在性，加载顺序/ABI仍需实机核对。

Image/Nat01 f1128b91…、naton0001.cpio.gz e75f7dbe…、native-once.efi 26493e0c…。
包审计只改init取消自动注册，最后私有凭据成员保持；新启动器指向独立Nat01/新initrd，
保留screen_only/nohibernate和显式native标记，无trial参数。正常载荷与默认不提升。
三个候选文件已部署到独立SurfacePmAuto目录，远端SHA/gzip及原ESP27项全部通过；
仅部署文件，没有安装菜单/once.env、重启、人工注册或进行native实机请求。
部署结果保存在native-once/stage-result.txt；当前运行仍为已通过两轮恢复的SPI trial候选。

下一步假设：已恢复的SPI路径可支持Android自身发起的一次睡眠；保留合法应用锁，
与原shell请求比较。变量：显式native入口及既有桌面AC宽限/实验保活分支，内核只接受
一次、人工开启；输入与SSAM诊断保持。区分观察：native attempts、实际machine_suspend/
唤醒源、SPIready与用户持续输入；未进入也需采集具体锁而非强制清空。停止条件：一次
准入被消费或120秒窗口无请求就撤销owner；失败立即取证，禁止自动重试。


### 2026-11-04：原生单请求候选启动验收，许可未开放

假设：新诊断内核与既有完整Android运行包兼容，取消init自动注册能保持启动无休眠许可。
变量：Nat01/单请求准入与native实验分支；区分观察为实际内核note/配置、模块note、
运行init/JAR、owner和native attempts；启动或输入异常立即取证并回原入口，不开放许可。

用户确认在场后一次性启动，boot e7638f55-1b00-40d4-bcd3-7be25ddede89；运行内核
note SHA2dc18660…与新vmlinux一致，/proc/config.gz与构建配置逐字一致。有效init、
三助手、两manifest、JAR及SPI/SSAM模块SHA匹配；两模块loaded ELF note也匹配。
JAR d872e26f…实际绑定到Android system/framework，SELinux标签system_file，当前Enforcing。
实验保活日志SKIPPED_NATIVE_AUTOSUSPEND，没有自动注册日志。实际UID1000 suspend-service
PID2649及可执行hash518d1319…匹配。screen_onlyY/nativeY/testN/trialN、owner/token0，
pm_test none/debug0/times0，native attempts0、success0/fail0，SPIready/count0/bus_error1。

物理ALSA卡/AUDIO_READY、音频HAL实际进程hash774cd7e7…、三remoteproc attached、
两个用户的FUSE挂载通过；这是结构检查，不是新的音频听感或视频解码验收。用户确认
已到桌面、触摸和键盘正常。Android PID system_server3883/surfaceflinger3393。
原菜单已原子恢复，ESP27项全部匹配，10份once.env均0，下次普通重启回原正常入口。

只读Awake检查仍有充电后台作业/Checkin锁，原生WakeLocks和Display blocker保持；
全局强制禁锁false/集合空，实验shell保活已无。未改应用锁、充电状态、锁屏或校准。
尚未开放owner、关屏或进行native睡眠，后续须准备有界采集再现场协调。证据保存在
native-once/boot-{audit,extra,kernel,menu-restore}、runtime-verified、loaded.config及notes。


单次原生观察脚本已准备并仅上传到RAM，尚未执行；SHA3166940b…，主机/目标shell语法
及单注册/无power-state直写检查通过。它限定当前boot/内核note与SPIcount0，使用独立
trace实例采集组件/SSAM/电源键，调用原owner helper一次，普通关屏请求保留应用锁；
一次消费或120秒窗口结束撤销owner，退出清理自己的trace挂载，不自动重试。
内核120秒只是准入期限，不能保证睡眠后定时唤醒；必须现场物理按键恢复。
当前AC在线/电量100%，已请求用户拔充电器并确认下一轮按提示135秒后按键配合；
尚未收到该确认，保持owner0/ready，不发休眠请求。


### 2026-11-04：首次native请求到达，prepare后中止，未进入machine_suspend

用户确认拔充电器、键盘盖接上且在场；实读AC/USB/Wireless/Dock全false、电量100%。
同boot e7638f55…只注册一次UID1000服务2649；准入消耗后owner0，collector/cleanup0。
612.799574开始请求，prepare612.873654→613.992663，随后dpm_suspend仅约52微秒退出。
1904条组件记录仅952prepare/952complete，均返回0、无trace丢失；没有实际suspend/resume
设备回调、没有machine_suspend，SPIcount0/bus_error1/ready保持，不能称真实休眠通过。

期间SSAM键盘盖HID事件持续，前后内核wake统计BAT1 event17→22/wakeup0→5；event0
（动态映射为045E:09AE键盘）event0→2/wakeup0→1，event1是045E:09B0键盘。
这些变化符合prepare阶段有新唤醒事件，但本轮未开wakeup_source追踪/PM debug，
不能唯一归因于键盘或电池，更不能将旁路缺IPA固件日志当已证实的中止原因。

Android记录attempts0→6/fail0→6：第一次获准后提前中止，后五次因一次性门已关闭
直接拒绝，无第二次设备挂起。最后errno-95来自保护拒绝，不能当首个中止的errno。
内核有些设备失败或早醒的通用提示；本轮没有组件非零回调，下一步仅增可观测性，
不放宽单次门、禁应用锁或改SSAM策略。用户按原135秒节奏检查后反馈“没有问题”。
后读同boot/同Android PID、Awake、owner/token0、pm_test none/debug0/times0、SSAMenabled；
原ESP27项匹配，自有trace挂载清理。只说明界面/输入正常，非睡眠后恢复证明。

证据：native-once/native-single-1、native-single-responses、native-wakeup-map与
native-first-final.txt。第二份观察脚本已准备并上传但未执行，只增加wakeup_source
activate/deactivate和临时PM debug（退出恢复原值），仍等现场配合再运行。


### 2026-11-04：第二次native单请求进入machine_suspend，SSAM唤醒，现场输入待确认

用户确认继续，保持电池供电/键盘盖连接；同boot/同模块，只增加wakeup_source追踪和
临时pm_debug_messages，新的native-single-2目录只注册一次。实际准入后进入
machine_suspend890.541094→918.896652，即28.355558秒；2338条组件回调全返回0，
无trace丢失，覆盖真实suspend/resume阶段。SPIcount0→1、bus_error1→2，全零报头
进入Error Handler后ready，IPTSd日志TOUCH_RESUME_REINITIALIZE count=1，Android
system_server3883/surfaceflinger3393保持。kernel stats success0→1，native attempts11→18、
failed11→17；一轮成功，其余请求被已消耗的一次性门拒绝，不是多轮睡眠。

内核记录IRQ173触发唤醒，trace见ssam_wakeup；首个SSAM事件为SAM/BAT cid0x16/iid1/
len0，源码解释为电池状态BST更新。machine_suspend结束前另有[timerfd]激活，debug
也报告两个active timerfd；不把电池事件顺序当唯一物理唤醒根因证明。event19经运行时
映射是Volume Buttons，属于恢复阶段事件，未证明操作者按了音量键。没有本轮PMIC
唤醒IRQ，不能称电源键直接结束睡眠。

collector/cleanup0，owner/token0，pm_test none/debug0/times0，SSAMenabled，trace挂载
已清理，原ESP27项匹配。采集及随后实读Android仍Asleep，等待原135秒协调按键后的
持续输入反馈，不软件亮屏、不重复注册。native统计total suspend time仍0，和trace
时间分开记录；本轮未采前后cpuidle驻留，不能称整段SoC低功耗或能耗通过。
日志为native-once/native-single-2、native-second-responses、native-second-state.txt。
第一次prepare中止本轮未复现，增加追踪不能当修复；SSAM最终策略、原生稳定性、
直接PMIC唤醒、多轮/能耗和SQ1验证仍未完成。


现场验收闭环：用户对原135秒按键/持续触摸与键盘检查反馈“恢复正常”。随后只读核对
仍为boot e7638f55…，Android Awake，mLastWakeReason=1，PMIC IRQ216计数2→4，
mLastWakeTime1131167ms晚于machine_suspend结束918.896652秒；这明确是SSAM先结束
内核睡眠、稍后电源键亮屏，不能标为PMIC直接结束native睡眠。SPIready/count1/bus_error2
保持，Android PID3883/3393未变，owner/token0，pm_test none/debug0/times0、SSAMenabled，
原ESP27项全部匹配且10份once.env均0，临时trace已清理。记录native-second-final.txt。

本候选的Android原生单请求真实睡眠及随后持续输入恢复，现获本机本轮验证；仍不提升
正常入口，不启连续自动休眠。下一步应优先区分首次prepare中止的事件来源与SSAM/
定时器提前唤醒策略，再做有界重复验证；1.1秒诊断延时不是量产修复，单次许可造成的
后续-95是实验门行为，不应据此修改应用锁或宣称原生框架故障。能耗/SQ1仍未验收。


### 2026-11-04：按用户要求切换正常Android策略（原型限制仍在）

用户要求保存工作、开启全部正常化休眠/锁屏策略并推远端；随后明确同时部署验证、
时间恢复系统默认。持续native现获授权，正常入口不再使用单次120秒门。实施保持
screen_only以保护未注册进程，只由原生UID1000服务持续申请s2idle；fallback仍screen-only。
恢复stock框架/SSAM，去除实验JAR和1.1秒诊断延时，不替换正常应用wakelock策略。

比较假设是原生持久准入与stock组件可维持正常运行；改变为持续gate、去掉两处诊断策略，
观察启动/模块/owner、正常锁屏时间与真实输入/自动休眠，出现恢复失败即回原载荷并记录，
不把多处变化后的行为差异唯一归因。实际Image86d896…在QEMU两次获准且可撤销；
普通root请求仍拒绝，原screen-only freeze/mem/disk拒绝。16项power/defaults/once/debug
测试通过，实际打包只改6项有效条目，私有身份成员最后且不变；原stockSSAM保持。

当前SQ2Power候选一次性启动boot a77ea624…；运行init511cf1af…、SPI模块/loaded note
匹配，nativeowner2690，lab日志NO_LAB_WAKELOCK，JAR126efcf2…/SSAM773f191c…，
声卡/AUDIO_READY/HAL774cd7e7…、remoteproc attached、FUSE通过。用户默认时间恢复
操作已执行；框架关屏15000ms、锁后设置null（使用framework默认），sleep_timeout-1、
stay_on_while_plugged_in默认1（系统自身充电策略）。锁服务IsLockScreenDisabled false、
WindowPolicy showing/inputRestricted/enabled true，凭据NONE未创建/清除。CLI get-disabled
仍true，记录此差异并等待UI证明。当前nativeattempts0，保留后台应用锁，不能称正常包
实际休眠已通过。原始正常/Mesa载荷保留；准备在现场反馈通过后提升同一候选菜单。
