# KGSL/SDE：先构建启动流程，再进行真机功能启动

## 当前活跃开发状态

- 分支 `codex/kgsl-sde-port-v83`，目标 active。用户明确允许开发设备操作，但要求先构建完整启动流程，不进行预期失败的试启。
- 最近确认的早期 SSH **root@192.168.5.122**，匹配内核 `6.18.60-sq2-kgsl-sde-mst-dev1`，boot ID `00ff60cd-658e-4564-976a-73462739207f`。PID1 为 BusyBox `/init`，不是已经完成 Android handoff。后续只读 SSH 复核超时 exit255；不能据此断言重启，也不能把先前状态当成刚重新确认。
- 用户要求保持这次启动继续接力，不返回 native 收尾。恢复脚本已停止；诊断自动重启 subshell PID528 经 cmdline/PPid/wchan/boot ID 确认后 SIGSTOP，已观察 `T (stopped)`。不要恢复该计时器，不要杀 PID1。
- one-shot 已消费，`next_entry` 空；默认 native 与其 Image/initramfs/DT/EFI loader 不变。默认入口尚未切到 KGSL/SDE。

## 已发生的试启，不能算功能成功

用户要求“先构建启动流程”时，旧部署链已经发出重启；最初仅凭没有 exit 记录判断尚未请求是不充分的，后续 SSH 和照片已纠正这一点。不得把停止本地主脚本等同于撤销已发出的远程重启。

当前实际 KGSL59 模块 SHA256 `3c56e35475c3e42fd13678b1dec82bc90616ab25023b363a983eddfb9d9d6a11` 与主机新构建匹配。真实日志：

```text
GPU OPP 177000000 Hz missing peak bandwidth: -61
probe with driver kgsl-3d failed with error -61
```

模块注册成功，但没有 GPU driver link 或 `/dev/kgsl-3d0`。SDE 模块也只有注册，没有匹配当前 `qcom,sc8180x-mdss` DT 的 probe。照片不是 kernel panic 证据，也不是 KGSL/SDE/Android 成功证据。

实际运行 FDT 保留全部十档 177/256/315/405/461/500/514/590/661/700MHz。当前失败发生在 `kgsl_bus_get_opp_table()` 读最低频率的 peak=0，早于 GMU bind 和取得 ICC 路径；不能用删高三档规避。

## 主机优先的启动顺序

机器可读状态图：`configs/kgsl-sde-boot-flow.json`。

```text
VERIFY_INPUTS -> VERIFY_DT -> BUILD_BOOT_PAYLOAD -> VERIFY_HOST_BOOT
  -> DEVICE_STAGE -> ARM_ONE_SHOT -> BOOT_ONCE
  -> BIND_PROVIDERS -> ANDROID_HANDOFF -> RECORD_RESULT
```

前四阶段任何必需契约不满足：`STOP_BEFORE_DEVICE_WRITE`，不设置 one-shot，不调用 reboot。
当前候选运行期失败：捕获真实错误、保持已经消费清空的 one-shot、保留现有 SSH、至多一次有界网络/SSH尝试，然后停止、不重试图形、不自动重启。自动恢复已验收载荷并返回 native 不是本候选已实现的行为，不能据旧设计文字声称已发生。
当前用户要求保持早期 SSH 调试窗口，不自动结束本次开发。init 前 panic/网络不可用仍需独立恢复方案，SSH helper 返回0不等于实际可达。

### 真正需要闭合的契约

1. **KGSL 十档政策**：显式 indexed 表、每档 default/min/max、OFF 与平均带宽约束。原厂族参考只有七档；590/661/700MHz 是项目实验 OPP，没有已确认的带宽政策。不得插值、填 max、假称 OEM 校准或删档。
2. **SDE 顶层所有权**：真实 MDSS root IRQ/domain、DMA identity、child populate、connector/component 生命周期；不能只改 compatible 或双重 IRQ demux。
3. **SC8180X 六接口 catalog**：包含 INTF5 内屏 eDP，旧 shrike 四接口 donor 不够。
4. **eDP/PHY/AUX/panel**：当前 generic PHY 与 legacy PLL/catalog/AUX 不能同时写 analog 寄存器。仅替换 PLL prepare 回调不闭合 lane/电压/训练所有权。
5. **clock/ICC/PM**：实际路径、单位、依赖顺序及失败收回；optional 缺失不是成功投票。
6. **构建/载荷**：全模块、MODPOST、vermagic、depmod、成对源码/内核与 initramfs 逐记录变化精确；禁用 HDCP/fbdev/secure-VM 等既有边界仍保持。
7. **真实启动控制**：`prepare-native-gpu` 成功必须表示真实 driver 绑定和节点就绪；它失败时父 init 必须停止 Android handoff 并保留早期 SSH，不能只打印失败继续启动。

## 本轮已完成的主机工作

- KGSL59 只增加缺 peak 的具体 Hz/errno 日志，不改变 `-ENODATA`、OPP、ICC/HFI 行为。独立原/新/回滚同输入均 `errno=-61 count=0 opp_puts=1`；只有新副本输出具体频率。真实 ARM64 完整 bus TU 三态全0。
- 新鲜59源109文件，60对象（59条 `-Werror` 编译+1链接）、3模块、MODPOST通过；stderr空。旧源/模块及匹配 KOUT 五项 hash 保持。
- indexed96、12个编译后运行反例及既有 bus99回归通过。初次模型缺 dev_err 定义失败保留，只补真实日志叶子，未改产品或断言。
- 原始 DT 与 Round5 本地/设备候选均通过新 raw-FDT 门禁实际检查：原始十档仍因政策缺失拒绝；两个旧候选因删三档拒绝。设备旧候选还改变三条 regulator 原始字节，不能继续沿用。运行 FDT 与加载文件存在 EFI/EL2 fixups，差异不等于损坏；该离线不变性门禁不能原样当 runtime FDT 等价检查。
- 18项 DT 模型测试通过；正向十档测试政策只是 synthetic fixture，不是可部署政策。
- SDE native root-resource guard 四角色单独保留（41模型场景、4反例、ARM64窄TU），**未集成/不部署**，不把拒绝错误输入当完整移植。
- 主机启动阶段及父 init 两文件事务已冻结并独立重跑：34项stage负控、12项init块、4项TSENS、2项实际stage→init联合用例通过；原版/回滚prepare失败仍handoff，新版失败保留SSH并停止handoff。仍不是完整497模块/Android启动验收。
- 原四角色位于 `round6/boot-stage`；独立Lead重跑在 `round6/boot-stage-lead-replay`，补充审阅 `BOOT-STAGE-INDEPENDENT-REVIEW.json`。原源码/默认builder未换，新的WIP补丁和可复建源构造器分别为 `patches/wip/kgsl-sde-boot-flow-20261111.patch`、`scripts/prepare-kgsl-sde-boot-flow.py`。
- 已实际构建独立源包 `/mnt/spx-workspace/chromebookOS/work/qualcomm-618/sde-device-20261111/round6-boot-flow-source`：stage、init、静态ARM64 FDT helper；状态固定 `HOST-CANDIDATE-ONLY-NOT-ACTIVATABLE`，不生成批准文件，不是完整initramfs。
- FDT helper在HOST/ARM64 qemu-user/当前真机只读执行，静态指纹逐字一致；13项ASan/UBSan解析测试通过，记录数65536/文本总量32MiB上限阻断路径膨胀。明确不把EFI/EL2动态fixup当DT损坏。
- 真实initramfs BusyBox在隔离非特权chroot/qemu-user中，用真实af_key模块验证：exact-name不建缓存，alias生成`modules.dep.bb`，重复alias缓存hash一致；内核插入为ENOSYS/exit1，不冒充加载成功。不是完整497模块前导阶段验收。
- 完整模块树的主机QEMU前导测试另行补充。最初测试命令漏掉已确认的`lsm=selinux enforcing=0`，在KernelSU初始化处Oops；纠正命令后进入init。随后自制测试init漏`busybox --install`造成命令127，已保留失败并补回真实init的applet安装。不能把这两项主机测试构造错误计为KGSL/SDE产品故障或成功。
- QEMU v3以真实497模块树跑到前导阶段：small-modprobe先装SSAM、报两次缺`crc_itu_t`，再补依赖后最终0；`dummy`因CONFIG_DUMMY=n报缺模块。基于真实ELF/depmod元数据生成拓扑计划后，v4已实际插入16个模块（15个可用请求，另含CRC依赖），零unknown-symbol；dummy明示可选能力不可用，不尝试、不伪造成功。新stage因没有真实批准停在图形加载前。该测试为自制QEMU前导init，不是整个Android init/实际SQ2 probe验收；生成器/执行器四角色继续审阅版本与并发边界。
- generic-PHY owner foundation作为新独立.c/.h已实现和Lead重跑，54场景/802断言、UNCERTAIN23断言、8个编译后运行反例；真实完整新.c及caller ARM64编译通过。此前允许provider不支持率和powered几何变更的v1反证保留并已纠正。只支持实际四档，powered rate/lanes/SSC变更在provider前拒绝；power_on失败保留资源、不假称已退休。源码WIP补丁`patches/wip/sde-generic-phy-owner-20261111.patch`，没有接入legacy parser/catalog/AUX，也没有配套模块或硬件验收。

## 续接入口与证据

### 当前组合源 v7（历史阶段结果不覆盖）

- 外部新目录 `/mnt/spx-workspace/chromebookOS/work/qualcomm-618/sde-device-20261111/round6-composed-boot-source-v7` 已构建并重开45个文件hash；`HOST-CANDIDATE-ONLY-NOT-ACTIVATABLE`，不含真实批准，不部署。
- 组合顺序：原始两脚本 → boot-stage → stage argv → review3 module-prelude → init-early-ssh 增量。公开构造器 `scripts/prepare-kgsl-sde-boot-flow.py` 对post-prelude精确父/新hash检查并拒绝未声明文件改动；生成计划的父init保持原件，不倒写其来源来迎合新init。
- `diagnostic_attempt()` 在当前PID1 shell先标记once，网络 `timeout -k 5 120` 后独立 SSH `timeout -k 2 10`；普通fail保留调用状态，graphics失败保留gpu_rc。debug失败后不重试，nondebug正常路径不额外占用Wi-Fi。记录 `reachability=unverified`，无假READY。只限制前台helper等待，不承诺清理所有已daemonize服务/不可中断任务。
- 局部普通fail同输入B/M/R均终端停止/nohandoff，网络/SSH次数0/1/0；27项HOST/窄ARM64 BusyBox命令及忽略TERM的真实-k缩放检查通过。初始harness语法2作为工具错误保留。
- 模块前导实际ELF `.modinfo depends` 与depmod图交叉验证，plan/module/loaded notes身份+跨进程锁+严格argv。真实内核QEMU v6b完成16唯一模块插入、零unknown-symbol、实际510模块文件+117固件检查；这是v6自制前导init范围，不提升为v7完整init/Android验证。
- 规范化仅在复制树移除build/source链接与派生缓存、物化两条已有固件别名，497个.ko和固件目标bytes不变；没有固件升级。完整输入库存已组合到源包。
- 构造器10、状态图8、DT18、FDT helper13（ASan/UBSan）、模块规范化7、固件规范化8，共64项回归通过。状态图检查依然exit78/不允许设备激活，因为真实十档政策与SDE原生硬件契约未齐。
- v7完整原字节`/init`实际kernel QEMU两例完成：缺FAT在75行真实mount失败rc255→普通fail；六个明确synthetic EROFS+FAT、真实overlay/deviceinfo/policy/keystore bind后，176行真正stage因缺approval拒1，185行停止。真实network-up=1/start-ssh=0各一次，仍unverified；无handoff、未加载KGSL/SDE。原init仍PID1，observer只读打印RAM证据。host在87.148/107.177s后终止guest，不是init退出、Android成功或SSD路径验证。见 `round6/init-composed-replay/FINAL-REPORT.json`。
- 同四角色主链 `round6/boot-flow-composed` 继续扩展；旧v4/v6保存在 `V4-FROZEN`、`V6-FROZEN`。补充局部角色在 `init-early-ssh` 与 `boot-builder-post-prelude`，不能替代主链或原SDE20四角色。
- Lead独立真实tar解包→B/M/R→精确重应用通过。发现原 `apply-diff.py` 在上级可信Git worktree内可能Git返回0却跳过所有文件，随后chmod失败1；旧helper/四角色完整保留 `PRE-GIT-ISOLATION`。当前同名helper显式隔离Git发现与继承 `GIT_*`，实际inside-parent B/M/R=1/0/1，outside-repo与继承Git重定向也通过。该修正只影响离线重建，不改变init/Mtar/DIFF/ROLLBACK。原作者WSL root场景受Git dubious-ownership阻隔，不能把当时通过冒称可移植性证明。
- 已只读核对历史EFI输入：`start.efi`/`el2launch.efi`均指向SQ2Sde，内嵌UTF-16 options与文本逐字一致，`lsm=selinux enforcing=0`各一次。只是原始输入检查，不批准旧诊断initramfs/不完整DT，也不表示v7 EFI载荷已组装。
- 当前继续构建独立HOST-ONLY v7整个initramfs：以真实历史runtime而非QEMU observer root为父，最小覆盖v7 runtime/规范化模块固件，检查完整delta与独立解包。该打包步骤不产生production approval，不组EFI、不激活、不改默认入口。
- 用户已明确：先启动流程，暂不制定590/661/700MHz实验政策。十档保留，硬件批准保持缺失；不以此为理由反复真机负例试启。

- 活跃状态：ignored `out/kgsl-resume-20261111/CURRENT.json`，以其中 `.122`/boot ID/计时器状态为准。
- ignored `out/kgsl-resume-2/round6/122-STATE.txt`、`122-runtime.dtb`、`122-kgsl-dmesg.txt`、`122-TIMER-HELD.txt`。
- ignored `round6/kgsl-bus-diagnostic` 原四角色和 `round6/build-corrected/RESULT.json`。
- ignored `round6/sde-native-resource` WIP 四角色；原 SDE20 四角色保持不替换。
- QEMU 只验证 virt 假分区上的 boot-path，预期停在 security-overlay；超时124与该标记如实记录，不能当真实 SQ2 PHY/MMIO/显示验收。

所有 full_* 保持 false；当前真实提交/fence/DDR投票/物理SDE显示/PM/性能/SQ1-SQ2 均未验收。私有载荷、设备日志、原始照片与凭据不提交。
