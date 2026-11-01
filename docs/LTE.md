# Surface Pro X SQ2 LTE 开发

2026-11-02 开始，分支 `codex/lte-support`，基线提交 `73ea6ee`。
独立工作目录 `googlebookos_on_surface-lte`；原主工作目录的未提交改动保留原处。
目标为 192.168.8.194 当前完整 Android/EL2 环境，不从精简 initramfs 推断硬件限制。

**状态：2026-11-02 用户要求暂停，已停止开发与硬件实验。LTE 尚未实现。**
下次继续从下述 EFS 缺口开始，不原样重复 online 请求，也不部署尚未验证的 EFI 草稿。

## 暂停时的新增结果

1. 更正 remoteproc 映射：MPSS 是 `4080000.remoteproc`，固件为
   `qcom/msft/surface/pro-x-sq2/qcmpss8180.mbn`；`8300000.remoteproc` 是 CDSP。
   首轮把后者写成 MPSS 的记录不成立，以下表格已更正。
2. 核对有效 `wifi-services` 与 `el2-rmtfs-start`：开机为 `/run/rmtfs` 创建
   2 MiB 零文件，RMTFS 使用该目录；`modem_fs1`、`modem_fs2`、`modem_fsg`
   SHA256 均为 `5647f05ec18958947d32874eeb788fa396a05d0bab7c1b71f112ceb7e9b31eee`，
   并实际与 `/dev/zero` 比较确认全零。当前没有接入本机真实 EFS/NV 数据。
3. 单变量 DMS online 实验：先读原模式 2，再请求模式 0；固件返回
   `result=1 error=52`，libqmi 将 52 定义为 `DeviceNotReady`。
   随后恢复模式 2 的请求成功，回读 `RESTORED_MODE=2 restore_failed=0`。
   因首次写请求即被拒绝，没有进入30秒 online 观察阶段。没有复试其他模式、
   reset、NV 写入、APN 配置或数据呼叫。
4. 前后 boot ID 仍为 `836903c9-eda6-449e-8295-e4401fe660b9`，guard Y、
   Android boot completed=1，wlan0 仍 UP/LOWER_UP。错误52本身不能证明空白
   EFS是唯一原因，也不能据此判断射频/天线/SIM不可用。

### 本轮代码与验证边界

- `sq2-lte-online-test.c` 是独立的手动限时实验工具，不扩展只读工具的写能力。
  只接受既有 factory-test 状态；请求 online 后，成功或失败都尝试恢复原值并回读。
  模拟测试覆盖成功、写入后丢回复、明确拒绝、错误初始模式及中断。
  ARM64 静态构建通过，实机拒绝与恢复路径已验证；online 成功路径未实机验证。
- `sq2-lte-efi-snapshot.c` 与 `build-sq2-lte-efi.sh` 是**未部署的 EFI 草稿**。
  目的为在 EL2 前一次性枚举小容量 BlockIo/GPT，按明确 EFS 名称只读导出，
  然后链入字节保留的原启动器。ARM64 EFI 编译通过；QEMU、GPT 边界及交接
  尚未测试，未上传到 ESP，未安装 ARM 开关，也未替换启动器或重启。
  能否通过此接口读到 Pro X 的真实 EFS 尚未知，不称为已经实现存储访问。
- 所有输出与实验原始记录在忽略目录 `out/lte-online-20261102`；设备的
  临时探针副本在暂停收尾时清理。原始日志、固件、校准数据不提交到远程。

### 资料与恢复开发入口

[Surface Pro X EFS issue #2](https://github.com/linux-surface/surface-pro-x/issues/2)
及其 [Wi-Fi 文档](https://github.com/linux-surface/surface-pro-x/wiki/WiFi) 记录了
使用 dummy EFS 的背景；Windows remote-fs 驱动中的 `modem.fs1/fs2` 也是后续
需要核对的来源。不能把其他设备的文件当成本机校准数据使用。
[SCM storage 补丁讨论](https://patchew.org/linux/20260126-scm-storage-v2-v2-0-fa045c7e7699@radxa.com/)
提供固件管理 NOR 的实现线索，但其已测试平台不是本机，且报告 EL2 下访问失败；
不直接移植写/擦除接口。先验证读取接口和本机分区来源，再替换运行时 EFS 副本。

本机 NVMe 已核对：BC711 512GB/CYA5N050012105J77，ESP B027-0512，
marker `SQ2-INTERNAL-512-20261027`，root loop0 来自 p3。
现有 GPT 是 mica 移植布局，没有 Windows modemst/EFS 分区；本轮仅查元数据，
没有修改磁盘。已部署启动器/菜单哈希仍为：

- SQ2Default launcher：`dc7b5ac95097533ed131f280d476e45a68a9c0280c758acd94a966348e651476`
- SQ2Fallback launcher：`1e6da8d09afee823c448dbb4ad01285572cf936f593c9246d066321c8e1be39c`
- GRUB：`9f1ee01e43da1a769514ef6e344e7d7d69ffaa21db88d8870eb8a082f1d635ef`

## 首轮实机基线

boot `836903c9-eda6-449e-8295-e4401fe660b9`，Android user10，
`suspend.screen_only=Y`，正常 EL2。首轮只有按需执行的只读 QMI 查询；
未修改内核、DT、固件、Android 属性、启动包或默认菜单。

| 层次 | 观察 | 尚未证明的部分 |
| --- | --- | --- |
| 固件与控制通道 | MPSS `4080000.remoteproc` 为 attached；DMS/UIM/NAS/WDS/DPM/WDA/IPA 服务均可由 QRTR 枚举 | 服务存在不代表射频、数据通路或 Android 集成可用 |
| DMS | 固件 revision `200717-CA-ceeab88-c8.1-00082.0`；Get Operating Mode 返回 2，即 factory-test | 该模式的来源、可否进入 online 及稳定性未验证 |
| SIM | UIM 返回 1 个卡槽，首卡 present，1 个应用 | PIN/运营商/套餐状态未查询，未读取 ICCID/IMSI |
| NAS | registration=0，CS/PS attach 均为 2，selected_network=0，radio interface 为 0（none） | 当前未注册且未附着；不能判断覆盖、SIM 授权或正常射频模式下的行为 |
| WDS | packet service status=1，disconnected | 未发起数据呼叫、未配置 APN，未验证蜂窝网络流量 |
| Linux 数据通路 | 仅 lo/wlan0/ip6tnl0；有效 DT 只有 IPA SMP2P 子节点，没有 IPA 硬件节点或平台设备 | IPA MMIO/中断/时钟/内存/GSI 通道及匹配配置仍待核对 |
| 内核驱动 | `CONFIG_QCOM_IPA=m` 且 ipa.ko 已打包；模块未加载，无 SC8180X compatible；`CONFIG_WWAN` 未启用 | 不能把加载现有 ipa.ko 当作完整修复；也不能从 WWAN 选项缺失单独推断 IPA 不可用 |
| Android | 未发现标准 Radio HAL 实例、rild/qcril/netmgr；vendor noRil 目录含排除 telephony 的 XML；airplane_mode_on=0 | 排除文件存在不等于框架实际已读取；需核对属性、init、VINTF、feature 和依赖后再接入 |

首次口头将模式 2 称为 offline，核对 libqmi 枚举后已更正；offline 实际是 3。
此处及探针均以原始值与协议定义为准。没有向固件发送切换模式命令。

## 已实现和验证

复用 `probe/test2/sq2-qmi-readonly.c`，增加 NAS Get Serving System（0x0024）、
DMS 模式名称及限制原因字段，保留原 UIM/WDS/revision/audio-domain 查询。
LTE 输出只包含状态，跳过位置、运营商和身份 TLV，不增加任意 QMI 写命令。
解码前检查响应头、事务、完整 TLV 边界、唯一 result、原始 error 和必要载荷；
QRTR 接收端还检查来源及截断。未知或畸形响应失败退出，不伪造成功。

`test-lte-readonly` 每次重新发现 node0/instance0 的服务端口，增加 NAS 查询；
可用 `SQ2_QMI_READONLY` 指定临时二进制，不覆盖系统现有工具。
保留该平台既有的 node0/instance0 选择；不要将其作为其他平台的自动选择算法。

独立构建和测试无需音频依赖、SDK 或网络：

```sh
python3 tests/test_sq2_qmi_readonly.py
bash scripts/build-sq2-lte-tools.sh
```

测试覆盖实际 C 解码器：全部八种 DMS 模式及未知值、NAS/UIM/WDS/revision、
原 audio-domain 路径、缺失/重复/失败结果、事务和长度错误、短 TLV、
端点参数拒绝及身份/位置字段不输出。静态 ARM64 构建通过并在实机运行成功。

实测探针 SHA256：
`4beb426bda30a7ba4db1b5e4638c27ffac481d1cbd5cc65d20394cc29d9b6b84`。
原始输出和实验记录保存在忽略目录 `out/lte-baseline-20261102`，不纳入 Git。
工具为人工执行，不加入开机脚本或持续日志；测试后删除设备上的临时副本。
前后 boot ID 不变，screen-only 保持 Y。

协议参考为 libqmi 的 [DMS schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-dms.json)、
[DMS 枚举](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums-dms.h) 和
[NAS schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-nas.json)。
本次下载的原始参考快照及哈希保存在忽略的 sources 目录；它们不参与构建。

## 接下来的实现顺序

1. 核对现有 modem 启动参数和匹配固件/Windows 配置，定位 factory-test 来源。
   本轮可恢复 online 请求已获 error52，勿原样重复；先解决 EFS 来源并记录新证据。
2. 从匹配本机的资源/驱动资料补齐 SC8180X IPA/GSI 与调制解调器通道映射。
   先核对有效 DT、模块依赖、固件及通道，再做独立候选；不能冒用其他 SoC 的
   compatible 或仅复制 SDM845 配置。保留 Wi-Fi 的现有 MPSS/RMTFS/SMEM 路径。
3. 先验证原生数据通路，再实现和注册 Android Radio HAL/数据服务、权限、
   init/VINTF 及框架配置；只删除 noRil 文件或伪造 SIM 状态不足以实现 LTE。
4. 分别验收 SIM、网络注册、数据呼叫、分配的地址/DNS、实际蜂窝流量和 Android
   ConnectivityService。Wi-Fi SSH 连通不能作为蜂窝数据成功的证据。

每次实机变更前记录假设、唯一变量、区分观察和停止条件；连续两次未增加区分
证据即换方向。本轮未验证 LTE 上网、通话、短信、eSIM 或长期稳定性。
保留已验收的音频、图形、触摸、温控、FUSE、Root 插件和两个正常启动载荷。
