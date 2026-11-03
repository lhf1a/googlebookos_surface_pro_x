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
