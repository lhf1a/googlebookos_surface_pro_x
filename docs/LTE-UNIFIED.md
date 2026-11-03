# SQ1/SQ2 通用 LTE：新机 RF 初始化追踪

## 范围与当前缺口

目标仍为同一初始镜像在新配置 SQ1 和 SQ2 上自动完成 SIM、网络注册、PS attach、
蜂窝承载及 Android CELLULAR。历史 SQ2 已完成 LTE/Chrome 实机验收；不能以此
代替新 SQ1。新 SQ1 可读 SIM，但正常默认内核下 NV453=1、rfnv 目录完整枚举0项、
DMS接口0、mode2、NAS未注册/PS detached、signal error74。

## 本轮已取得的新证据

原 SQ1 Windows 的 `QcSOCPartition.sys` SHA256 `9c9087f6…` 与
`qcremotefs8180.sys` `1ad254e9…` 已重新取得并核对。后端静态链补全为：

```
RemoteFS 0x13870 / READ IOCTL ecaf2b46
  -> QcSOCPartition 0x3dfb8 -> 0x42bb0
  -> block branch 0x42e90 -> 0x42010
  -> read operation1 at 0x422c4 -> 0x3cd08
  -> service selector2 at 0x3d190 -> 0x3c840
  -> TrEE IOCTL 0x568004 / service AE865C08-4A07-404D-BE51-D9A0465E23E5
```

SCM调用号 `0x02001a01`、arginfo `0x214`、40字节命令与32字节details、
storage_type1及read1与[公开SCM storage补丁](https://lists.openwall.net/linux-kernel/2026/01/26/752)
相符。该公开线索已在旧记录中；本轮新增的是原Windows机器码与实机读取对应，
不是宣称首次发现该协议，也不是宣称Linux EL2已可直接使用它。
`inspect-surface-fsg-backend.py` 固定两原件SHA，核验26处机器码/常量及两个服务GUID。
地址均为RVA；不是REA伪xref或只按字符串推断。

通过原驱动固定FSG GUID的READ IOCTL，先4096字节头部、后完整2097152字节均成功。
新原件SHA256 `31c3e5af3de7240c6b40bbac05a0bdfa600bd2034ab34a20ce1fab64f5d81b6d`，
与当前共享包 `bd124b2d…` 不同；IMGEFSG后缀为0d而非0f，两者外层几何一致。
它来自另一台Windows参考SQ1，**不是已证明属于待测SQ1的校准**。
未绕过文件锁、停驱动、调用任意SCM、写NV、清FTM或提取身份/密钥。
高熵/格式/哈希差异不证明设备绑定、解密失败或RF内容有效。

## 可复用工具

- `scripts/Read-SurfaceFsgReference.ps1 -SelfTest`：Windows PowerShell 5.1固定请求布局自测。
- 在匹配原驱动的参考Windows设备上，显式 `-OutputPath PRIVATE_PATH` 才读取；
  固定FSG GUID、零offset、2MiB和单一READ IOCTL，不提供任意读写命令参数。
  私有输出拒绝覆盖；不要将输出放在公开仓库。此工具不是默认镜像组件。
- `scripts/inspect-surface-fsg-backend.py --soc-driver DRIVER --remotefs-driver DRIVER`：
  仅离线输出机器码校验JSON，不连接设备。
- `probe/lte/surface-lte-rf-observe.c`：有界20秒手动DIAG观察，NV白名单453，
  固定rfnv目录只计数；不写NV、不改日志mask、不reset、不自动进入普通启动。
- Radio轮询修复拆分 firstModem/firstNetwork/firstSignal 并隔离查询异常，
  解决重复UNAVAILABLE/注册通知被信号错误阻断；不是基带RF恢复。

## 本轮启动试验设计

已按用户要求归档并清理旧SQ1PmTrial ESP目录；原默认/回退载荷未改变。
先重启原默认内核，移除PM trial标志后仍复现相同RF故障，排除本次PM内核切换干扰。
仅FSG及校验文件改变的候选在本地生成；全部其他有效cpio条目逐字节一致，
凭据成员保持最后。普通Image/DT/固件/菜单两项保持；使用一次性GRUB env，
先消费并回读once=0才进入候选，下一次启动自动回原默认。

离线匹配解析的BASELINE/MODIFIED/ROLLBACK是FSG `bd124…/31c3…/bd124…`，
退出均0；回滚镜像hash与原件一致，补丁重建候选逐字节一致。
这不是硬件RF通过；实机结果以本节后续记录为准。

## 不重复的已完成路线

不再复试相同FS1/FS2配对、无变化online、QMI21 EPERM或错误DIAG握手。
不把NV1878 inactive、空/rf或PDC error16单独当失败根因。
私有二进制、FSG、原始日志、带登录配置的启动包、完整A/B/R与工具错误记录
留在原工作区 `out/lte-main-review-20261103/efs-restore-v1/`，不提交Git。
## 一次性候选实机结果

正常原默认启动 `e98867d8…` 与候选启动 `6dfff0a2…` 都正常进入Android；
新FSG精确哈希同时存在于`/etc/sq2-lte`与`/run/rmtfs`，RMTFS读取0:4096。
两者均NV453=1、rfnv0、DMS接口0/mode2、NAS0/PS2/signal74，观察器exit0。
没有出现LTE注册，**第二份FSG迁移未改善，不提升默认、不用于通用发布镜像**。
一次性env确实消费0，默认菜单恢复原哈希。ROLLBACK boot `4451de32…` 已回原默认；同一脚本仍得到上述RF状态，exit0。
原FSG恢复bd124…，默认/回退镜像与菜单hash一致，试验目录已清理，常亮锁重新持有。
不将参考SQ1数据冒充待测机自有数据；不把失败自动解释为设备密钥绑定。

本轮5项Python测试、PowerShell布局/缺路径/拒绝覆盖测试及原件26处机器码校验通过。
未将诊断程序接入常规启动，未把失败FSG放入构建输入。

## 早期 EFS 日志试验：有握手，但没有 DATA 日志

为避免再换相同校准输入，另建私有一次性候选，仅在 `wifi-services` 的
`el2-rmtfs-start` 前加入限时采集器及启动门；FSG 仍为 bd124…，所有其他有效
cpio 条目一致，原凭据成员最后。主机 ASan/UBSan、ARM64/QEMU 自测通过，
补丁可精确重建候选，离线回滚恢复原镜像 SHA。此工具没有加入公共默认构建。

正常 BASELINE boot4451de32… → 候选 bootd5727d78… → 实际回滚 bootc0c1b5f9…
均为 DMS2/接口0、NAS0/PS2、signal74；候选和回滚均 NV453=1、rfnv 完整枚举0项。
采集器在启动后约19.667秒完成 DIAG feature002051 握手，并发送固定 SSID9/mask15；
35秒窗口 `data_packets=0, efs_messages=0, malformed=0`，随后发送 SSID9/mask0，
NV/RFNV 观察完成并 exit0。RMTFS 仍读取 FS1/FS2 sector1 和 FSG 全4096 sectors。
这是实际无 DATA 的观察，**不是“EFS 没报错”、解密失败或设备绑定的证明**。

限制：发送成功不是 mask ACK；没有切换 DIAG buffering/real-time 模式；未实现 QSR4。
启动门与采集器共用日志文件的独立写偏移可能覆盖门时间标记，本轮该标记确实缺失，
因此不能据此证明日志启用早于首次 FSG 恢复。原 QDB 的 authenticate/decompress
字符串仅是模板，不能当成本轮真实错误。mask0 清理也不代表恢复未知的原 mask；
已用真正的正常重启退出实验，而非仅关闭程序。

回滚后默认镜像9721b3ad…、Mesa回退9fd377aa…、原菜单2776eded…及两套载荷
全部哈希不变。试验目录/启动采集器已移除，常亮锁重新持有，screen_only=Y、
声卡/AUDIO_READY/实际HAL af73f71e…保持。私有完整证据在原工作区
`out/lte-main-review-20261103/efs-logs-v1/`，扩展原四角色记录。

晚期 GET-mask 尝试未收到 feature，查询没有发出；随后这次早期启动虽完成握手，
但也没得到区分 restore/auth/decompress 的日志。按照连续两次无区分证据的规则，
停止在该路线盲加协议参数，转回成功 SQ2 与当前 SQ1 的有效启动环境对照。
本工作区缺 SQ2 当前可信 SSH 主机记录，已请求确认地址/指纹；不绕过主机验证，
也不以历史 SQ2 成功冒充当前对照。两机通用 LTE 初始镜像验收仍未完成。

## 有效启动环境交叉核对（不换件、不重启）

参考 Windows SQ1 的 DriverStore 中实际 `qcmpss8180.mbn` 为75470832字节、
SHA256 `9ec7b6c6dae5d97af84ab71af6cb93b1892a4db1bd2b0b9de3e12a5d691a5439`。
它与当前 Android SQ1 的 `/lib/firmware`、ESP共享 `firmware/` 中的 MPSS，
以及历史默认输入清单一致。因此目录名 `pro-x-sq2` 不是选错 MPSS 文件的证据；
这仍不证明固件已接受 FSG，也不能仅靠文件哈希证明运行固件每个内存页。

本地 common 镜像重新计算得到957b5ee6…，正是历史成功 LTE 打包器固定输入，
不是另找一个“相近”基线。按 cpio 最后覆盖顺序解析，与当前部署9721b3ad…比较：
RMTFS、el2-rmtfs-start、pd-mapper、tqftpserv、qrtr-ns、MPSS均相同；
wifi-services 唯一差异恰好是历史 LTE 打包器插入的 seed 行，重放该插入逐字节一致。
qcom_q6v5 与 qcom_q6v5_pas 两模块也相同。没有把未包含的模块算作通过。

历史已验收 IPA DT49228267…与本次读取的 SQ1 live FDT 对比 MPSS、RMTFS、IPA
及其内存引用/子节点：50项属性完全相同，差异仅4项预启动完成/状态属性。
live MPSS complete1/stage11、RMTFS complete1/stage4；RMTFS仍为
0x85500000/2MiB、client1/VMID15。运行进程exe哈希85668122…与包内一致，
参数 `-r -o /run/rmtfs -v`，实际打开 `/dev/qcom_rmtfs_mem1`。
解析器结果与fdtget交叉核对；不把这些状态当作RF初始化成功。

当前SQ1 bootc0c1b5f9…前后不变、guardY、DMS2/接口0/NAS0/PS2/signal74；
本轮没有修改启动载荷、NV、射频设置或重启。完整私有证据在
`out/lte-main-review-20261103/environment-compare-v1/`。

用户说明 SQ2 当前没有SIM；这不妨碍上述只读启动环境对比，但不能进行注册/流量验收。
用户提供组网地址并确认ED25519指纹，可信记录已保存在私有目录。第一次实际SSH
登录超时、邻居Unreachable；用户随后说明P2P链路切换并更正地址，同一已确认密钥
严格校验通过，实际机型SQ2、bootffad6382…，与SQ1的bootc0c1b5f9…不同。

### 双机实时对照结果

| 观察 | SQ1（有中国广电SIM） | SQ2（用户确认无SIM） |
| --- | --- | --- |
| DMS mode / capabilities | 2 / 0项 | 0 / UMTS,LTE（5,8） |
| NV453 / RFNV完整枚举 | 1 / 0项 | 0 / 14项 |
| NAS registration / PS / signal error | 0 / 2 / 74 | 0 / 2 / 74 |

SQ2 DIAG观察exit0、完整关闭目录、无NV写入；前后QMI及boot ID/guard不变。
其声卡、AUDIO_READY、实际HAL af73f71e…保持。没有清FTM、发online、设置日志mask，
也没有重启/改动SQ2启动配置。临时读取程序与DT副本已从SQ2 RAM目录清理。

双机live FDT所选MPSS/RMTFS/IPA及其内存引用/子节点**54项属性全部一致**，包含
预启动完成状态；运行MPSS、ADSP、CDSP、RMTFS进程exe、el2-rmtfs-start及
wifi-services均同哈希。两机FSG bd124…与零FS1/FS2的实际RMTFS文件也相同，
RMTFS日志均读取FS1/FS2 sector1和FSG全4096 sectors，实际设备fd均为mem1。
seed自身的机型/身份准入版本不同，但已观察到相同FSG进入RAM；不以此为由删除保护。

因此无SIM不能解释SQ1的RF NV/接口为空，当前差异在网络注册之前；不能再盲换
已一致的IPA/MPSS/启动顺序。文件相同而恢复结果不同仍不构成“设备密钥绑定”的证明。
下一步检查本机安全存储/恢复条件：复用已核对Windows固定READ与SCM后端结构，
先核对只读ABI及边界，不重试两份旧FSG、不复制运行SQ2的RF NV、不开任意SCM写入口。
双机注册、PS和真实蜂窝流量的通用初始镜像验收仍未完成。

## 本机安全存储 READ ABI：离线实现与原机器码对照

本轮不连接设备、不修改NV/固件、不重启。原件检查器
`inspect-surface-fsg-backend.py` 从26处扩展至68处机器码/常量核验，新增
`storage_read_layout` 输出。原Windows READ链确认：命令40字节、details32字节，
GET_INFO操作4/输出105字节、READ操作1/每次至多4096字节；数据指针/剩余长度
按字节推进，但LBA每块只加1。高层FSG GUID已经在上层解析，SCM命令中的
slot/lun/guid_ptr均为零，不能直接把FSG GUID塞进这个底层指针。
布局与[公开SCM storage v2](https://lists.openwall.net/linux-kernel/2026/01/26/752)一致。

Windows TrEE返回包还分别检查首个u64为1、偏移8处u32为0；不要把它与Linux
封装后的`scm_res.result[0]`或未来EFI协议返回数组直接等同。返回语义、物理内存
所有权/映射及cache维护仍是接入真实传输前必须独立核对的门槛。

新增 `probe/lte/surface-scm-storage-read.h` 是**纯编码器，不是已能读设备的驱动**：
只允许GET_INFO/READ，拒绝INIT/WRITE/ERASE/DEINIT和其他操作；验证元数据容量、
物理页对齐/溢出及LBA边界，仅接受4096字节块几何。1GiB是本地防护上限，
不是实测芯片容量；不打印GET_INFO内的序列号等标识。没有SMC/EFI调用、磁盘I/O，
也没有加入正常启动。原GPT只读解析器保留，尚未把新编码器接成完整FSG读取链。

除host ASan/UBSan和ARM64/QEMU边界测试外，私有QEMU夹具运行了固定原件
RVA0x22a58的原始ARM64请求构造函数：仅context lookup和memset使用环境桩，
没有运行原存储、TrEE或SCM函数。GET_INFO及READ LBA0/1/8191四组中，新编码器
与原函数的72字节请求和32字节参数完全相同，8字节边界哨兵保持。
这证明对应输入的编码一致，不证明Secure World接受或本机RF可恢复。

同一原件输入的检查器事务：BASELINE26处/无新字段 → MODIFIED68处/有新字段 →
ROLLBACK26处/无新字段，全部exit0；原SHA恢复，补丁重建逐字节相同。第一次patch
因CRLF归一化失败，保留错误后改为保留原换行的binary patch，未改变原件。
三项针对性Python测试通过；完整私有源码夹具、测试二进制与A/B/R留在原工作区
`out/lte-main-review-20261103/storage-read-v1/`，扩展既有四角色。

下步只接入有界、只读的EFI GET_INFO传输并核对返回/内存协议；在离线验证完成前
不做新启动实验。即使GET_INFO成功，也必须从本机有效GPT解析FSG范围，再进行
受限读取，不能猜LBA或拿其他设备的RF NV补齐。通用LTE目标保持未完成。

## 2026-11-03：优先复用 SQ2 成功完整载荷；SCM 仅保留离线工具

用户追问“SQ2 已走通，SQ1 参考成功路径做成通用为什么不可以”。可以且应当以成功
SQ2 路径为基线。目前54项选定DT属性、相关固件/RMTFS/seed结果相同，只证明这些
已核对项相同，不代表完整EFI/内核/有效initramfs/Android载荷和启动环境全部相同。
SQ1 RFNV0、接口0与SQ2 RFNV14、接口5/8是已观察差异，不是设备绑定或必须另写
SCM后端的证明。没有证据判定SQ1不能复用，也不把新存储后端作为通用LTE的前置要求。

下步优先取得成功SQ2实际选择的完整启动链/载荷，并与SQ1及已验收归档逐项对照；
复用已完成的54项及固件检查，不再原样重测。随后以成功路径作单变量候选，保留
SQ1必要的板级适配、共享固件、正常/回退和screen_only保护，进行SQ1一次性验证。
若仍失败，再按区分证据定位；不先假定密钥绑定、不重复两份旧FSG、不复制运行SQ2 RF NV。
通用验收仍要求两机全新配置、SIM、NAS注册、PS及实际蜂窝流量；SQ2现无SIM不能冒充验收。

本轮已开始的GET_INFO工作已收口为未接入默认启动的离线工具：
`spx_scm_get_info` 针对固定EFI协议、EL1、独占低4GiB两页，仅发一次操作4，不发
INIT/READ/WRITE/ERASE；调用前clean+invalidate，之后invalidate-only，校验元数据、
边界哨兵、105字节信息/几何，擦除缓冲再释放。结果只保留几何/状态，无序列号。
采用保守completed=1/full-return-slot=0策略；不是本机EFI存储返回已实测的声明。
同步固件调用不可由软件超时抢占，“有界”仅指次数与内存，不保证墙钟时限。

固定qebspil 8e4d9e6 EFI头/缓存源码哈希校验，vtable偏移56、参数10/结果4槽
编译断言通过。真实transport源码+环境mock的17种场景及EL/revision/null准入在
host ASan/UBSan与ARM64/QEMU输出一致；release含真实CurrentEL/cache指令，仅生成
可重定位object，不是可启动EFI镜像，不是安全世界或LTE实机测试。
`spx_storage_check_info` 原件/候选/回滚同输入为response_validation=0/1/0，均exit0，
回滚原SHA和补丁重建一致；4项针对性Python测试通过。初次unused-function、cache
STATIC编译配置错误已修正，原失败与完整命令输出保留。没有设备连接/配置/重启。
私有证据在原工作区out/lte-main-review-20261103/scm-getinfo-v1，扩展原四角色。

## 2026-11-03：SQ2 完整启动链对照与 LTE 组件复用已在 SQ1 实测

按用户要求优先复用成功路径，不接入新SCM后端。严格主机校验下读取运行SQ2的实际
GRUB/launcher/DT/initrad04及运行文件，并与SQ1比较：
- Image c1ef559f、qebspil e3675e36、tcblaunch 195997cf一致；launcher仅7处路径字符串
  替换后逐字节相同，无另一个启动实现。完整DT仅8项机型/CPU-GPU OPP属性不同。
- system/vendor/product/system_ext/system_dlkm/vendor_dlkm六个实际只读映射分区
  全量SHA256相同，不只比较build fingerprint。未读取或改写userdata。
- 按最后条目生效解析完整initramfs，SQ2 1044项、SQ1 1073项，46路径不同；多数是
  存储扩容、KSU、身份保护、显示属性或凭据。LTE相关7项并非全部与成功包相同。
  Radio与rmnet的代码段哈希不同；LTE JAR内部文件全部相同，仅容器字节不同；
  overlay差异含APN XML/签名。不能把所有二进制hash差异都称为功能差异。

只把成功SQ2的7项LTE组件（rmnet、Radio HAL、overlay、JAR、APN及两个校验表）
替换到SQ1原镜像的副本，其他有效条目逐字节不变，原机型/存储保护、FSG、凭据最后
成员、音频、DT、内核、共享固件与正常/回退载荷均保留。候选d41fb299…，离线补丁
重建/回滚hash、校验表及菜单语法通过。不是把整套SQ2磁盘布局硬套到SQ1。

实际 BASELINE bootc0c1b5f9… → MODIFIED boota854b90e… → ROLLBACK boot17d11376…：
同一QMI观察均 DMS2、capabilities接口0、NAS registration0/PS2/LTE0、signal error74，
观察均exit0。真实运行Radio hash f99b9d6c→58aa52c2→f99b9d6c，rmnet也实际按包切换，
候选达到Android boot_completed1、LTE_RADIO_TRANSPORT_READY。没有恢复信号/注册；
**不采纳这次组件替换为修复，不再重复相同组合。** 这不证明SQ1不能复用LTE实现。

一次性env实际消费0；真正回原默认后，原菜单2776eded、默认9721b3ad、回退9fd377aa
及保护清单全通过，试验目录已清理。screen_only=Y/nohibernate、常亮锁重新持有，
物理ALSA卡/AUDIO_READY/实际HAL af73f71e保持，不冒充新增听感验收。
最终只读UIM GetCardStatus error0/cards1/card_state1/app_type2/app_state7，确认USIM
仍就绪；不读取ICCID/IMSI/PIN。SQ2全程只读、未重启、bootffad6382保持，无SIM事实不变。

本轮附加DIAG握手features0，未读出新NV453/RFNV；旧NV观察不可冒充本轮结果。
保留错误：第一次SCP源目录错误(-1，停止对应传输后修正)、DIAG观察子命令3被外层df
的exit0遮蔽（改用同一QMI观察完成A/B/R）、新boot的Android /tmp与SSH /tmp分离导致
127（将同hash工具放入/proc/1/root/tmp后重跑成功）、SIM查询需Android namespace，
首次lookup失败1后纠正。均未通过忽略错误来宣称测试通过。

结论范围：已补齐完整静态启动链/只读系统与LTE组件组合对照，复用成功软件组合
不足以消除当前注册前故障。后续应区分本机固件RF初始化/FSG接受条件，而不是继续
改APN/IPA寄存器或重复此组合；UEFI版本SQ1 4.234.140、SQ2 8.256.140不同只作事实，
不是根因证明或跨机型刷UEFI的理由。不清FTM、不写NV、不复试旧FSG。
私有完整载荷、源码工具、原始输出与A/B/R在原工作区
out/lte-main-review-20261103/boot-chain-v1，继续扩展原四角色。通用初始镜像仍未完成。

## 2026-11-03：RMTFS 当前共享窗口只读核对，前移观察点

在上述实际回滚后继续使用原基线；未再重启或替换任何LTE载荷。补齐了此前文件hash
没有覆盖的运行时观察。两机实际sysfs均为class rmtfs、client1、phys_addr0x85500000、
size0x200000、char243:1。核对Linux6.3.8的rmtfs_mem.read接口及本项目probe-only补丁后，
通过O_RDONLY读取固定2MiB，两次哈希；只在设备管道内处理字节，不保存/导出原始内容。
[驱动原始接口](https://raw.githubusercontent.com/gregkh/linux/v6.3.8/drivers/soc/qcom/rmtfs_mem.c)。

SQ1 boot17d11376：最后记录的数据操作仍是FSG read0:4096。两次AP侧窗口hash均
为eb51ef4c…，与供给文件bd124b2d…不同。逐字节比较只输出差异计数/范围：
2,088,453/2,097,152字节不同，覆盖全部512页；前后数据请求序列hash相同。
**这是当前共享缓冲不同，不是已证明首次交付错误或cache故障**；仍需排除Modem
在收到数据后自行改写。该窗口也不能当作FS1/FS2的进程内shadow。

SQ2 bootffad6382：当前更完整日志已经包含多轮FS1/FS2 write0:4096，先前12KiB
抓取未覆盖后续写请求。当前窗口两次d8ed5a72…稳定，前后数据请求序列hash相同；
由于已发生后续写，不要求它等于初始FSG。backing FS1/FS2仍全零，不等于Modem
运行EFS相同或未生成NV；-r下RAM shadow的旧采集记录见LTE.md，不冒充持久落盘。
两机RMTFS进程/原exe hash85668122、boot ID、screen_only=Y和Android完成状态保持；
SQ1前后QMI仍DMS2/接口0/NAS0/PS2/signal74。临时只读脚本均已从设备移除。

独立历史审计没有发现遗漏的清FTM、FCC解锁或MCFG强制选择步骤；首次成功记录为
FSG恢复后NV453自然0/RFNV14，再正常online。旧shadow工具不在现有Git/本地证据中，
不能猜旧地址复用；现有文档也不足以独立证明bd124具体采自哪台设备，不能据此反驳
用户提供的来源，更不能推断设备绑定。

下一步针对同一原RMTFS交付调用，在共享写完成与应答之前观察输入/readback一致性，
先锁定原件导入符号、离线验证观察器再做一次性候选；不重写LTE后端、不清FTM、不
写NV、不复试旧FSG/七项组件组合。同步读取或诊断观察可能影响时序，须作为实验限制。
私有证据：原工作区out/lte-main-review-20261103/rmtfs-buffer-v1；扩展原四角色。
首次猜错sysfs类名的读取exit1后，按实际设备枚举修正；未跳过任何读取错误。
通用SQ1/SQ2初始镜像的注册/PS/真实流量验收仍未完成。

补充限制：本次两机RMTFS stdout文件恰为8KiB/16KiB；可能存在用户态输出缓冲。
因此“前后请求日志hash不变”只表示可见记录未变，不能保证窗口期间没有尚未刷出的请求。
后续交付观察必须使用独立、直接写出的记录，不能再依赖日志文件当前尾部推定准确时序。
### 原 RMTFS ELF 的交付调用已锁定（仅离线分析）

从已校验镜像只在内存解析实际原件856681222851149a54eead466804cb4bc51e5aece26a0b0afb41ae231403a62b，
69360字节，确认导入pwrite/pread@GLIBC_2.17，无pwrite64/pread64。2MiB请求不是一次大写：
0x2da4调用共享写helper0x4210，每次512字节；正常char-fd路径0x429c尾跳pwrite@plt，
每次返回必须等于512才继续。完整循环结束才编码msgid3并经sendto应答。
因此可在真实pwrite返回后、返回调用者之前只读核对该扇区，而不用替换RMTFS后端。

下一候选须过滤实际char设备而非所有pwrite，聚合4096个连续512字节扇区，保留原参数、
返回值及errno；只有完整序列且输入hash等于已知FSG才标记FSG交付。sysfs和普通文件的
pwrite不属于本观察目标。原件另有base非空的memcpy路径，单纯hook并不覆盖它；两机
当前maps无设备映射且fd3指向mem1，只支持采样时的char-fd路径，不证明启动全程覆盖。
必须以实际捕获完整序列作为新实验准入/覆盖证据，不能以“观察日志为空”判定写入失败。

原件有stdout/printf/fwrite导入，无setvbuf/setbuf/fflush导入；结合8KiB/16KiB日志长度，
更不能把可见尾部当作最后真实操作。观察记录需独立fd直接write、仅输出hash/计数/状态，
不导出内容、不重新执行原写、不改变NV。以上仅确定可观察调用点；观察器尚未实现或部署，
没有新增LTE成功结论，AP侧readback也不能证明Modem侧可见性或固件接受结果。

## 2026-11-03：原 RMTFS 的 FSG 逐扇区交付已在 SQ1 完整捕获

新增手动实验室工具 `probe/lte/rmtfs-delivery-observe.c`，不是默认LTE实现或新存储后端。
只对固定原RMTFS85668122显式LD_PRELOAD；校验原件/实际sysfs/字符节点，在真实pwrite
返回后逐扇区只读回读，保存并恢复原返回值/errno，原调用只执行一次。独立新建0600日志，
只输出hash/计数/状态，不输出NV内容；完整记录须以record_end=1结束。固定4序列/
32768调用/8MiB预算，递归/竞争/短读/日志失效为inconclusive，不改变原写结果。
保存log/read fd身份，发现复用不读写或误关他人fd；这是固定非对抗原件的受控观察，
不声称fstat消除了所有跨线程TOCTOU，也不把次数有界说成可中断驱动调用的墙钟超时。

`python3 scripts/verify-rmtfs-delivery.py --output out/rmtfs-delivery-check`
实际31项host ASan/UBSan与ARM64/QEMU输出相同，涵盖SHA、完整覆盖、错误/短读、
错序、输入非FSG、并发/递归/预算与fd复用。生产库默认opt-out真实libc pwrite的
BASELINE/MODIFIED/ROLLBACK均 `pwrite=512 errno=123 readback=512 equal=1`、exit0。
原ELF+原loader/libc下QEMU加载验证通过，GLIBC_2.17 pwrite确实绑定到观察库；
安全invalid-option路径三阶段输出相同/exit1（原件不支持-h，不是设备功能测试）。
兼容性仅覆盖实际原glibc，不声称适用于所有旧glibc。原测试夹具缩进编译失败已修正留档。

只在SQ1镜像副本改变两项：EL2 helper最后的exec增加显式env/preload，以及新增观察库。
非EL2分支、原RMTFS、FSG/seed、Radio/rmnet、固件、内核/DT、音频和最后凭据层不变。
候选4bfe6373…；库2338d645…；有效cpio逐项核对、补丁重建、独立副本的原ROLLBACK.sh
恢复原SHA通过。一次性菜单保持两项，消费env为0并重新读回确认后才进入候选。

实际 BASELINE boot17d11376 → MODIFIED bootbf0efa22 → ROLLBACK boote70d6a67，
同一QMI/安全观察三次exit0：均DMS2、接口0、NAS registration0/PS2/LTE0、signal74。
候选运行原RMTFS hash不变，maps及库hash证实加载，不是只在打包文件里加入工具。
直接独立日志完整捕获：coverage1/fsg1，4096次512B写与4096次回读、各2097152字节；
input与readback SHA均bd124b2d…，differences0、first_difference-1、real/read return512，
本次观察区间124238907ns。**证明这个观察窗口内AP侧逐扇区写后读回一致，不证明
未经观察的时序、Modem可见性、认证/恢复成功或RF初始化已完成。** LTE仍未恢复。
因此不能再把上轮事后共享窗口差异本身当成AP复制错误，不盲改cache或重写RMTFS。

已真正回原默认，原菜单2776eded、默认9721b3ad、Mesa回退9fd377aa及保护载荷全通过；
观察库和日志在当前root不存在，RMTFS无预载，SQ1LteRead目录已移除。screen_only=Y、
nohibernate、常亮锁、ALSA/AUDIO_READY及实际audio HAL af73f71e保持；无新增听感声明。
回滚后独立只读UIM仍card_state1/app_type2/app_state7（USIM ready），工具随即移除。
本轮未做新NV453/RFNV读取，不把旧boot的值当新结果；SQ2未连接/重启，仍按用户无SIM。

下一缺口更精确：原日志iovec `(0:0)` 在encode/sendto之前打印，仅是响应字段。
精确原件0x4e88调用sendto@GLIBC_2.17，其真实ret/errno尚未直接捕获；helper失败
被压成-1，上层strerror(-ret)不等于真实errno。下一次只给同原路径补齐与FSG序列关联的
QMI响应type/msgid/transaction/result/error、目标QRTR元数据与真实发送返回（不记录payload）。
ret==len也只证明本机传输接受，不能跨越到基带接受结论；不重复已有交付试验或旧FSG组合。
私有镜像/补丁/命令原始输出在原工作区out/lte-main-review-20261103/rmtfs-delivery-v1，
扩展既有四角色，原文件保持。通用初始镜像的双机注册、PS、真实蜂窝数据验收仍未完成。

## 2026-11-03：SQ1 原 RMTFS 响应发送接受已实测；结束 AP 传输疑点路线

在原逐扇区观察器上增加真实sendto的返回/errno与最多26字节参数的安全只读解析；
只输出QMI/QRTR元数据，不输出原始报文/NV，不重发、不改包/参数/返回。固定原件、
实际socket类型/域/地址与日志fd身份仍校验。新增recvfrom仅作进入下一次接收前的
关联失效边界，不读接收内容，且不在可能阻塞的真实接收期间持观察锁；封住原FSG
编码失败、下一请求回复误继承pending的路径。仍明确request_transaction_verified=0，
不是请求txn全链比对或Modem确认。safe copy仅用本进程mem固定fd，坏指针/短读为inconclusive。

保留31项并扩到67组，host ASan/UBSan与ARM64/QEMU输出一致。生产opt-out真实
socketpair三阶段均sendto14/errno123/收到14/recv_errno124/equal1、exit0；原pwrite
转发结果也不变。原ELF/原loader下pwrite/sendto/recvfrom@GLIBC_2.17绑定均验证。
源码副本的原版→候选→实际ROLLBACK.sh response模式恢复，sendto_observer为0/1/0，
相同实际收发均成功；补丁重建再次1且源码hash匹配。旧image回滚模式再次执行仍恢复9721…。
verify-01在并发源码改动时被最终hash门正确拒绝，最终固定verify-02；没有把中途结果当最终通过。

SQ1原基线先实测新准入API：仅自建无bind/send/recv的QRTR socket，确认域42/类型2/
地址长12；/proc/self/mem仅读本helper自己的14字节合成常量，未碰他进程/Modem内存。
一次性镜像7e789242…仍仅改EL2 helper最终exec和新增观察库5769a70f…；其他有效条目、
原RMTFS85668122、FSG/seed/Radio/rmnet/固件/内核/DT、最后凭据层都保持。

实际 BASELINE boote70d6a67 → MODIFIED boot85fc9124 → ROLLBACK boot7112ff09：
同一QMI/安全观察均exit0，均DMS2、接口0、NAS registration0/PS2/LTE0、signal74。
候选真实maps/hash证明加载。完整日志记录4096×512B，input/readback均bd124…、差异0；
紧随同线程且中间无后续接收边界的响应：type2/msgid3/transaction1/result0/error0，
目标QRTR node0/port16，send_length14/send_return14/send_errno0，transport_accepted1。
记录完整record_end1。**本次AP侧完整写后读回与本机QRTR发送接受均有正证据，仍不是
Modem收到/接受FSG或RF初始化成功的证明。** 不再继续增加AP交付日志，不把未联网归到
本轮未发现失败的这条链，也不因此改cache、重写RMTFS或复试旧FSG/组件组合。

已真正回原默认，原菜单2776eded、默认9721b3ad、Mesa回退9fd377aa及全部保护项通过；
库/预载/临时工具已移除，SQ1LteResp已清理。screen_only/nohibernate、常亮锁、
ALSA/AUDIO_READY/实际音频HAL af73f71e保持，无新增听感声明。回滚后只读UIM仍USIM ready。
本轮无新的NV453/RFNV读取，观察器不加入默认构建，通用LTE功能仍未完成。

转向既有接口的固件环境对照：SQ1正常boot7112ff09实读soc_id340/revision2.1/family
Snapdragon；未读serial_number/unique_id。当前Android未挂debugfs，qcom_socinfo详细
平台/运行image版本接口未暴露；SecureBoot/SetupMode变量未暴露，保持UNKNOWN。
本轮不为此挂载/加载模块/重启。第一次PowerShell文本管道给最后参数附CR造成exit1，
保留错误并改用已保存LF脚本执行后exit0，未跳过失败。SQ2两个既有可信地址均超时，
未绕过主机验证；本地现存归档也未找到SQ2实际socinfo字段，不能拿DT标签补值。
下一未完成项是恢复可信连接后的双机实际平台身份/运行固件build对照；差异仅线索，
不是设备绑定、校准来源错误或不能通用的证明。仍需两机带SIM注册、PS及真实蜂窝流量验收。
私有完整镜像、源码差异、回滚、字面命令输出在原工作区out/lte-main-review-20261103/
rmtfs-response-v1，继续扩展原四角色，保留原始字节和此前所有已完成结果。

## 2026-11-03：SQ2 新启动与只读平台缓存字段对照

SQ2 在已确认主机密钥下恢复连接，本轮实际boot为1df5aab6…，
不是沿用旧ffad6382的观测。用户称无SIM，本次UIM也为card_state0/apps0；仍DMS0、
接口5/8，NAS registration2（搜索中）/PS2/LTE1，signal error0，原查询值
rsrp=-97、rsrq=-14、snr=42。SQ1同一正常boot7112ff09…为card_state1/apps1，
仍DMS2、接口0、NAS registration0/PS2/LTE0、signal error74。**这次SQ2无SIM也能
查询到信号，不是本轮注册、PS附着或蜂窝联网通过；SQ1的RF未就绪问题未解决。**

两机现有sysfs均soc_id340/revision2.1/family Snapdragon。通过已校验的现成QMI
helper和既有查询逻辑，实际运行MPSS的DMS revision两机逐字相同：
`200717-CA-ceeab88-c8.1-00082.0  1  [Mar 05 2020 23:00:00]`。
本次有效MPSS、RMTFS、el2-rmtfs-start、wifi-services与供给/run目录FSG哈希也相同。
Radio/rmnet仍有已知版本差异；其所在七项SQ2成功组件组合已在SQ1实际试过且无改善，
不重新进行相同替换。运行版本字符串相同不等于固件内部状态或整个启动环境相同。

随后只在新unshare、private传播的子mount namespace，将已注册debugfs以
ro,nosuid,nodev,noexec临时挂载，读取现有qcom_socinfo驱动缓存的固定非唯一标量。
同一字段读取命令的BASELINE/MODIFIED/ROLLBACK为：挂载前ABSENT → 私有只读视图
可见 → 显式umount后ABSENT；这是接口可见性的事务，不是LTE功能A/B/R。
两机可见的8个值全部相同：

| 缓存字段 | SQ1 / SQ2 实测共同值 |
| --- | --- |
| info_fmt | 0x0000000f |
| raw_version | 2 |
| hardware_platform | 29 |
| platform_version | 65536 |
| hardware_platform_subtype | 0 |
| chip_family | 0x00000057 |
| raw_device_family | 0x00000006 |
| nmodem_supported | 255（只保留字段原值，不解释为实际Modem数量） |

`oem_variant`两机均ABSENT，保持未知，不填默认值。各机父namespace及Android的
mountinfo SHA256在自身操作前后均不变（不声称两机mountinfo彼此相同），boot ID不变；
前后DMS模式保持SQ1=2、SQ2=0。收集和私有视图脚本均exit0，已显式卸载，退出子
namespace，并复核清除对应临时工具/路径；没有持久挂载、加载驱动、重启、改变模式或写NV。
未读SMEM469/image表、raw NV、serial/unique等身份字段，也未修改默认/回退/固件。

保留并修正错误：最初引用不存在的`/bin/test-lte-readonly`，sha256sum失败exit1；
改用已pin的现成helper和既有查询逻辑后完成exit0，不宣称首次命令成功。私有视图脚本
准备时另有PowerShell正则转义错误，未执行WSL语法命令；改为字面String.Replace后
真实语法检查及两机执行通过，原错误留档。

结论仅限这些已读取字段与运行版本没有解释双机RF状态差异，不能推出UEFI全同、
校准绑定或某个固件内部根因。之前AP侧交付/应答正证据仍有效，不回头盲改cache/RMTFS；
不重复旧FSG或七组件组合。本轮没有新的NV453/RFNV读数，通用初始镜像的双机SIM、
NAS注册、PS及真实蜂窝数据验收仍未完成。私有字面输出/退出码、脚本和文档事务位于
原工作区out/lte-main-review-20261103/socinfo-resume-v1/online-20261103-231900。

下一步仅从已存QDB的`rfcard_data_init`、`rfc_dynamic_load`（HWID/FSID）及
`rfc_common_nv_item_read`（RFC NV读取/解压）模板出发，追本轮已复核hash9ec7…的
原MPSS中可信Hexagon引用和选择条件；若遇分页/CLADE，先验证映射。模板不是实际
错误证据，不重复DIAG mask或AP交付探针，也不将静态入口当作已找到根因。

## 2026-11-04：RFC 静态锚点核对与反汇编覆盖边界

在私有目录从已核对APT包隔离恢复LLVM18.1.3及依赖，未安装到系统；已实际验证
Hexagon目标，固定原MPSS为9ec7b6c6…、75,470,832字节，ELF32 little-endian、
e_machine164/e_flags0x66，采用hexagonv66。依据固定版本
[LLVM Hexagon实现](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-18.1.3/llvm/lib/Target/Hexagon/Disassembler/HexagonDisassembler.cpp)，
按packet/immext完整语义核对，不把孤立数值当指令交叉引用。

生成fc476b24…的53节**仅分析视图**：只改e_shoff/e_shnum/e_shstrndx并追加元数据，
33个原PH、31个PT_LOAD文件字节及原映射字段完全不变；独立校验和精确重建通过，
错误SHA、截断及目标/原件路径碰撞均拒绝。NULL、高熵段和NOBITS尾部不标作已解码
代码，存在的原VA重叠仍保留。不能部署、签名或替换设备固件。

**无SHT并不阻止本次LLVM反汇编。** 同一64字节窗口0xc0800000..0xc0800040的
BASELINE/MODIFIED/ROLLBACK/REAPPLIED实际均exit0，16条指令的字节及文本逐条相同，
并与原PH2文件字节一致。原版/回滚通过PT_LOAD#2 fallback，带no-overlap warning；
视图/重施仅改为.ph02.load.rx.file标签且无该warning。真实rfc-elf回滚得到原9ec7…，
重施得到fc476…，均已哈希核对。命名节视图不是这项反汇编能力的必要前置。

原QDB4723a56f…在0x40后的zlib内容与既有解码文本逐字相同；原头明确是Qshrink4
`<hash>:<ss_mask>:<ssid>:<line>:<file>:<string>`，第一列不是源代码行号或文本行号。
确认7个完整的16字节描述符及其格式/文件名指针，原PH可映射的VA从0xc20d7fc0到
0xc20d8020；主锚0xc20d7fd0对应rfc_cmw.cpp:1312的构造函数日志描述符。
这不是源行1331的HWID比较分支，也还没有建立指令xref/调用参数流；相邻源行不能
证明同一控制流。五个目标QDB记录的完整前缀没有原始文件命中，不据此判断运行时缺失。

对11个原PF_X文件区间、共9,596,676字节的限定全-d已完成，输出84,077,435字节，
含52,207行unknown。针对已验证锚点的直接无符号/有符号常量及LLVM实际合成的
immext模式均无命中；这不覆盖所有寄存器组合、间接表、GP/PC相对引用或未解码页。
复用这份输出，另按固定外部[QSR4参考脚本](https://raw.githubusercontent.com/mzakocs/qualcomm_baseband_scripts/1bd919800813e26e830b30c481319fde3a6c983b/qshrink4_qdb_ghidra_script.py)
的`((u32_constant >> 3) & 0xfffff)`投影检查138,984个完整`##`常量，10项自测及
排除半截immext通过，仍无五个目标候选。该投影只是参考假设，**不是本MPSS已证实ABI**；
没有套用Pixel5函数地址/配置，也未执行外部设备提取器。

这些结果都是有明确覆盖范围的阴性，不证明RFC函数不存在、固件有故障、SQ1校准绑定
或LTE根因已定位。没有解压/验证CLADE，没有设备连接、启动、NV读取或固件写入；本轮
不会把静态工具通过称作SIM/注册/PS/蜂窝流量通过。通用初始镜像目标仍未完成。

工具错误已保留并修正：首轮下载文件名未考虑APT编码epoch，随后猜测libLLVM文件名
也失败；改用已下载实际文件名/依赖路径后隔离LLVM执行成功。内联WSL变量展开丢失
另有失败记录，改为固定绝对路径；section-view初版误拒filesz=0且offset超EOF的PH32，
已将文件范围检查限定非空段，未改PH或补造段字节。未把中途失败输出当作最终验证通过。

进一步对原件的限定标量邻域只读定位得到4组地址/范围候选，其中2组有较强结构关系：
PH21内相邻字段精确等于PH25的base/end；PH2三条连续记录的地址样字段覆盖PH24
末端向下0x2000对齐后的候选区间 [0xc88a0000,0xc88a6000)，未覆盖文件末端0xc88a6040前最后0x40字节。另2个孤立整数匹配已排除。仍未确认CLADE配置ABI、字典或运行时
使用，第三字段不能猜作PA/文件偏移；这不是解压成功，也不是压缩机制已完成识别。

新增原字节/现存指令证据：PH2的42条length/type启动记录不是已证明的硬件页表；
type4消费者确实读取source/destination/length并按4字节搬运。上述三记录将
[c88a0000,c88a6000)+r25搬到[057d2000,057d8000)；这是代码路径中的目标地址，
仍不能命名为CPU PA或特定本地内存。r25的定义为运行PC页减链接基址c0800000，
但中间调用是否始终保留r25尚未穷尽验证，没有观测实际运行值，不能固定代入PH的PA。
原候选三块各2048个word全部分别满足30/23/17bit，OR为3fffffff/7fffff/1ffff，
越界和零word均0；前邻0x6000与PH25首0x6000两个对照不满足。它加强字典候选的
结构证据，但未建立压缩页边界、解码器ABI、RFC调用或LTE修复，也没有进行解压。

最后对0xc0800a60→0xc08015e8完成一次有界静态核对：可达函数仅28字节/7个word，
与原件逐字相符，是将TLB索引0..0xbe清零的循环；没有type5、字典或压缩配置关联。
没有执行函数或观察运行时TLB，也不递归跟踪此函数。本轮收口为已证启动拷贝路径与
字典结构候选；下一阶段应从type5的真实消费者或压缩页结构另找入口，不能称已找到
解码器。不再裸搜hash或重复全-d，
也不能把公开CLADE说明当成本件已压缩/已解密失败的证明。固定参考源码、来源与边界在
私有PRIMARY_SOURCES_AND_LIMITS.txt和reference-source/provenance.json；完整脚本、
字面输出、二进制分析副本、回滚及文档事务位于原工作区out/lte-main-review-20261103/rfc-static-v1，
不提交原始固件/QDB、网络地址、凭据或原始日志。

## 2026-11-04：type5 CLADE 配置链与 PH25 诊断/断言元数据

本轮复用原MPSS 9ec7b6c6…及既有反汇编，已找到真实type5消费链：0xc08c9e68
明确分派type5，保存五word载荷后调用0xc08d26c4，写B+0/0x10/0x14/0x18/0x1c，
再设置syscfg bit20。原件诊断模板直接命名QURTK_clade_cfg_base、exc_hi_word和
clade_dict_word，不再仅凭高熵或相近地址推测身份。B的文件初值0x057d0000使
B+0x2000等于已证type4拷贝目的0x057d2000，0x1800 words等于0x6000字节；
但B运行值、调用路径是否实际到达、硬件是否启用均未观测。

固定Qualcomm官方hexagon-hypervisor commit `23d5bde4313c1e63ba64dd5d7f542b1055188ef8`，
7份只读源码均通过Git blob及SHA256校验。[官方寄存器定义](https://github.com/qualcomm/hexagon-hypervisor/blob/23d5bde4313c1e63ba64dd5d7f542b1055188ef8/libs/h2/common/h2_common_defs.h#L99-L138)
将上述五位移命名为REGION/COMP/EX_HI/EX_LO_SMALL/EX_LO_LARGE；
当前载荷编码值1/093c0000/00540000/00021300/09568700、字典偏移0x2000与3×0x2000、
syscfg bit20均相符。固件额外清除部分低位，只有当前对齐输入与官方右移接口逐项相等；
这不是任意输入等价、同版本codec ABI或运行时成功证明，官方配置头没有给出本件页格式。

另将PH25完整zlib流740,479字节只读展开为4,001,947字节私有分析副本e9dd92b7…，
EOF、无剩余输入、Adler、64MiB输出上限、独立重建字节相同均通过；已有目标拒绝覆盖。
它可解析出**诊断/断言元数据**，不是据此发现可执行代码，也不是PH24的CLADE解压。
首批字段为u16源行号/u32格式指针/u32文件名指针；已有原指令将bit.c:108的空值分支
与record 0xcfc0000a及调用静态关联。连续前缀在0x7ec2停止，共3245组有效字段；
这不建立整个文件统一10字节stride。其233项EFS筛选只有time_efs.c:224队列耗尽
模板获得直接常量xref，不是RF/FSG认证故障，未观测模板实际输出。

最终另按代码中真实`r0=##`引用而非猜测stride核对：域内7935项，7934项首10字节字段
有效、唯一地址7613；宽RF/EFS筛选43项，报告10项。得到实质RF静态锚：
0xc0b61d78读取`[r18+0x1a4]`并在零值时跳到0xc0b61e3c；随后r0指向
record 0xcfca2188（rfdevice_grfc_asm.cpp:268，模板提及NULL rfc_data_override_ptr），
r1来自`[r18+0xc]`，调用0xc089d250。小窗口原生反汇编及原件字节复核证明250保留r0/r1、
清r2/r3，再尾跳0xc089d1f8；248/258同入口、清参不同。只确认条件/调用/记录的
静态关联，**没有命名完整调用ABI或fatal语义，没有证明SQ1触发此条件或它就是根因**。

下一次唯一有界检查点是内部packet `0xc0b61d78`，追本函数内`[r18+0x1a4]`的来源、
设置和分支条件；它不是已识别函数首址，不继续裸搜hash或重复全-d。本轮无新设备访问、
启动、NV/射频写入或外部源码执行；通用LTE目标仍未完成，未新增SIM/注册/蜂窝流量验收。
字段键名误用及首轮shell退出捕获空值均已保留；改用实际instruction字段和subprocess
记录后验证exit0。完整原始证据、源码、分析二进制与本轮TYPE5文档事务仅留私有
out/lte-main-review-20261103/rfc-static-v1，不发布原固件、凭据、网络地址或原始日志。
