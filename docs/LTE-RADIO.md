> 2026-11-04：本文为历史记录。当前构建、自动本机 FSG 与产品清理见 [LTE-PRODUCT](LTE-PRODUCT.md)；旧实验工具已退出产品源码路径。

# SQ2 Android 电话框架集成

## 范围和当前验证状态

LTE03 已验证的数据链路见 [LTE-DATA.md](LTE-DATA.md)。本工作增加真实 Radio AIDL
适配，让 Android 电话框架负责订阅、APN、移动数据策略和 NetworkAgent。
这是单 USIM、LTE IPv4 数据设备；不声明语音、SMS、IMS、eSIM 或 5G 能力。
voice/messaging 服务仅补全调用契约，不支持的请求都完成对应错误响应。

2026-11-03：只读实机查询已通过 USIM READY、真实 ICCID/IMSI 与 EF_AD/SPN/UST
读取、NAS LTE 注册/PS attached、RSRP/RSRQ/SNR。测试不打印身份正文。
首次完整候选 rad02 已实际加载七个 AIDL V3 服务，SIM LOADED、默认数据订阅1，
功能声明和 `config_mobile_data_capable=true` 生效。物理 ALSA、AUDIO_READY、实际
HAL774、三个 attached remoteproc、EL2/screen_only 均通过。

rad02 未完成数据集成：NAS 运营商描述含未标注编码的非 UTF-8 字节，写入系统属性
导致 com.android.phone 崩溃一次；另一个缺口是桌面系统原 `android:xml/apns` 只有
version8 空根，没有当前 SIM 的上网 APN。原始错误和所有原始记录留 ignored out。
已按停止条件恢复 LTE03 菜单并实际回退重启（boot1db44682），HTTPS200 和原基线恢复。
这不是 modem/EL2 硬件限制，也不是已完成原生 LTE UI 的证明。

rad03 增加严格可打印 UTF-8 校验（不能解码的名称留空，由真实 SIM SPN/PLMN提供
显示依据），重新绑定/电源切换后的注册状态同步、NAS 选择模式只读查询及私有 APN
overlay。实际启动后 SIM LOADED、LTE REG_HOME 与真实信号进入框架，名称崩溃未重现。

随后定位两个框架交接问题：同一个 build ID 下 TelephonyProvider 不主动刷新 APN；
启动助手改为调用 `content delete --uri content://telephony/carriers/update_db`，按资源
checksum 合并并保留用户编辑/删除，不使用 restore 或直接清库。成功数据响应的
`pduSessionId=-1` 被 Java `DataCallResponse.Builder` 拒绝；AIDL 无 PDU session 的
约定值是 0。原始 Binder 异常已留存，NDK parcel 自身往返成功不能代替 Java 接收验证。

修正后的 HAL58aa52c2 在 boot c3999280 的 RAM 运行验证通过：实际 `/proc/PID/exe`
哈希一致，原生 MOBILE[LTE] CONNECTED，rmnet 地址/DNS/路由/MTU1496 正确；数据关/开
撤下及恢复网络和地址，完整 `cmd connectivity airplane-mode` 触发 DMS1→0并重连。
关闭 Wi-Fi 后默认网络变为蜂窝，curl HTTPS200、Chrome 新 URL 加载、RMNET 字节增加；
恢复后 Wi-Fi 重新优先。截图确认状态栏信号格/LTE、设置中的中国广电 SIM、移动数据、
流量统计、LTE 首选类型与 APN 列表。临时 callback 日志已删除，未修改用户权限提示。

rad04 完整启动 boot0aec94ae 验证 HAL58aa52、SIM/APN、原音频/EL2保护均正确，但真实
NAS 仍为拒绝/PS detached，自动数据未通过；已按停止条件启动 LTE03 做环境对照。
LTE03 boot ee296b3e 同样 NAS3/PS2、WDS error14/CM2001；NAS 自动选网仍开启，
LTE system-info 显示非本运营商小区 forbidden，reject-info-valid=0，不能编造具体拒绝原因。
用户确认位置/SIM/天线未变。一次正常20秒 low-power/online 后，原 LTE03 恢复 NAS1/PS1、
非漫游和 HTTPS200；没有修改 NV/APN/射频或选网参数。这提供了环境对照和恢复证据，
不能证明历史拒绝的唯一根因，更不能把拒绝归咎于 HAL。

同一份rad04随后在boot75246691独立启动，约61秒自动建立原生LTE、HTTPS200；提升默认
后普通启动boot1e6156a5再次自动联网/HTTPS200。两次均验证实际HAL58aa52c2、SIM LOADED、
ALSA声卡/AUDIO_READY/实际音频HAL774、三remoteproc attached、EL2与screen_only保护。
默认菜单直接启动该已验证载荷；不再依赖一次性标记。偶发注册拒绝仍是观察到的限制，
没有加入自动飞行模式循环或宣称长期稳定性。

## 实现

- `sq2-radio-qmi.h`：有界 QMI TLV、UIM card/EF、NAS registration/signal 解析；
  同一 Client 持有 socket，互斥 transaction；不含 PIN/NV/MCFG/reset 写命令。
- `sq2-radio.cpp`：七个 Radio V3 AIDL 服务。数据/power 操作排队，只有一个 bearer
  owner。`sq2-qmi-data` 保留经过实际验证的 DPM/WDA/WDS 顺序与 QMAPv4配置。
  未知 cell 字段用 Android unknown，身份只传框架；NAS 轮询只在变化时通知，不落 raw log。
- `generate-sq2-radio-defaults.py`：从固定 frozen API 生成全部不支持请求的完成响应；
  特别映射 hangupConnectionResponse，不能让 Binder OK 代替请求完成。
- `radio-stage`：仅独立候选的早期 RAM vendor overlay；安装 HAL、rc、功能与资源，
  最后发布 VINTF。`lte-framework-boot` 保留原 IPA/RMNET 准备，检测 Radio 声明后
  发布 ready 标记、请求 APN checksum 刷新后退出，不启动旧 Java NetworkAgent。
- 资源覆盖只启用移动数据，语音/SMS仍 false。私有 `xml/apns` 由实际 SIM 的
  operator numeric 和已验证 APN生成，TelephonyProvider 的资源 checksum/合并机制
  负责导入和保留用户编辑/删除，不直接改 SQLite、不伪造订阅。

## 构建

WSL Ubuntu24.04，参考 hardware/interfaces 固定提交
`1a56e38edc2f2f6189ef405ee1edce554e15cbc0`，Radio frozen V3、common V2；
复用已有 Android15 AIDL、NDKr28c/API35 与 platform binder NDK headers。
目标 `libbinder_ndk.so` 只用于链接平台服务符号，不打包替换系统库。

```
python3 scripts/build-sq2-radio.py
bash scripts/build-sq2-radio-overlay.sh OPERATOR_NUMERIC PRIVATE_APN_FILE
python3 -m unittest discover -s tests -p test_sq2_radio.py -v
python3 scripts/pack-sq2-radio.py VERIFIED_LTE03 RADIO_BUNDLE FRESH_OUTPUT
```

测试含 ASan/UBSan parser边界、SIM/PLMN/signal、实际失败文本编码回归、APN输入与
MNC宽度、frozen AIDL所有请求的响应覆盖。设备 idmap2 已验证 bool及APN覆盖可映射。
构建脚本不部署。包审计只有12个有效cpio项变化，其他项逐字节一致，密码SSH成员最后。
生成的 APK/镜像含单机私有配置，留 out/private，禁止直接发布。

2026-11-03 主机验证：Radio三项、启动zygote选择一项、MFS两项通过；数据/mode/RMNET
native与ARM64/QEMU selftest、Python compileall、相关 shell语法通过。全库 unittest
discover 运行139项，12个旧脚本导入时把 discover 当路径、1个未修改的触摸校准测试超时、
5项跳过；不声称全库测试通过。原始输出留 out，未为LTE改动顺带修改无关测试。

## 恢复与限制

候选先用一次性 GRUB env 验证，完成后已提升默认并再次正常重启。当前默认菜单
SHA `adc0afab1f569580388cee950ebd448eab08e177fc2a66ca42a51b381f1d13e2`，始终恰好
Qualcomm 默认与 Mesa fallback 两项。原默认、LTE03、Mesa、共享固件保留。
Radio04 init `368d4f213d3f5bd6c8d6be159443c569cc82c846da2a88d111b5f416edcc4d05`；
EFI `a50e65c904d73d87d86dd43ef6fe25298fe87eb1f32ded33c3efe3d39d0ab93a`；DT仍49228267。
恢复LTE03：核对磁盘/ESP/载荷身份后，将菜单恢复为SHA883cd0a5的已验证版本，直接
chainload `/EFI/SQ2LTE/lte03-once.efi`。该原菜单在ESP的
`/EFI/SQ2LTE/radio03-20261103/grub.before.cfg`，恢复前必须校验完整哈希，sync后原子替换。
ESP空间有限，只有本任务过期候选在本地完整备份哈希验证后删除；不删除工作回退。

当前 init rc 使用已授权的 permissive lab/kernel context，未完成生产SELinux策略。
PIN管理、SIM写入/逻辑通道、漫游实测、IPv6、语音短信与长期移动测试不在已完成范围。
