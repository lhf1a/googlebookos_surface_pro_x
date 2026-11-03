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
