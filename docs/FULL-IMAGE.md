# 原模板整包构建（2026-11-10）

## 临时启动期 SSH 调试入口（不改变正常构建默认）

取消哈希门槛的包已写盘，后续照片进入 Android 服务反复退出阶段：GMU GPU_SET 超时、
GPU 初始化 -110，SurfaceFlinger/hwcomposer 反复退出；不能把这些归结为单纯刷日志。
用户确认 SSH 不可达并授权临时提前启动 SSH。正常包的 ssh-framework-boot 等待
sys.boot_completed，不能用来调试此次启动未完成的问题。

`scripts/pack-spx-debug-ssh.py BASE OUTPUT` 在当前无哈希门槛包上追加诊断层：
在 GPU 准备和 Android PID1 交接之前启动既有 Dropbear、Wi-Fi 关联和 DHCP；
保持原 root 密码/密钥、SSH 主机身份和私有 Wi-Fi 配置，不开放免认证登录。
调试包暂时在 RAM init 配置中移除已知 Android Wi-Fi 服务声明，避免两个管理者抢接口；
不写 Android 保存的网络设置、不运行旧 wifi-rollback，也不在交接时停掉诊断 DHCP/supplicant。
仅维护直连 LAN 的路由规则，DHCP 地址输出到控制台和 /logs/early-dhcp.txt；不承诺旧组网 IP。
`sq2.ssh=0` 可让该候选走原普通启动分支。普通 full-image 构建不自动带此启用标记。

产物：`D:\spx_img\early-ssh-debug-20261110\initramfs.cpio.gz`（250866848 bytes）。
5 个有效记录变化，内核/模块/图形库/原单用户策略不变；这是调试通道，不是 GPU 修复。
已写入同一 USB SSD 的正常/救援入口，两个文件读回一致，原 DTB/启动器保留，硬盘已解除挂载并离线。
尚未证明实机 Wi-Fi/SSH 可达；未新增备份或执行回滚测试。

## 当前策略：取消启动与通用整包构建的文件哈希校验

用户在 IMG_0460 的启动反馈已确认磁盘识别通过，停止点推进到 `p010-renderer-stage`，
并补充日志 `libstagefright.so FAILED`。新增整包流程曾把原系统库的 BASELINE.sha256
误改成待安装库的哈希；用户随后明确要求取消这类哈希校验，而不是继续维护哈希门槛。

`full-image` / `disk-image` 现不再对 initramfs、固件、原模板/recovery、super 首部等做哈希对比，
不再重写运行时清单或生成 ESP/整盘 SHA256SUMS。源码构建仍执行真实 make/depmod，
保留 ABI、必要文件/格式、路径边界、磁盘唯一性和命令失败传播。固定输入目录和原 20 GiB 模板不变。
显式的历史 `verify/package` 审计入口不属于这条通用构建路径，未伪装为校验通过。

`runtime_boot_policy.py` 从独立私有输入生成无文件哈希门槛的启动 helper 副本，
不修改导入原件、不伪造 sha256sum，也不把复制/挂载/驱动失败改成成功。
音频仍核对实际服务进程与绑定文件的 inode；休眠仍核对服务路径、UID 和状态。
触控校准保留设备+屏幕+描述符派生的存储键（不是完整性校验）；校准 APK 每次启动安装一次，
同次启动内重启 helper 不再重复安装。APK 签名、SSH 身份、SELinux/权限策略未在本次放宽。

已生成 `D:\spx_img\startup-nohash-20261110\initramfs.cpio.gz`，需同份写入正常/救援入口；
仅 15 个启动 helper 和 1 份存储配置变化，Image/模块/库/固件/单用户策略保持。
原 v5 整盘文件仍为历史原件，未重新打包。无哈希门槛 initramfs 已写盘并进入 Android 服务启动阶段，
随后出现 GPU/服务循环，尚未进入桌面；临时 SSH 调试通道见上节。

## DT 源码导出损坏修复

第二张启动照片已进入 Linux，出现 `ldo10: invalid voltage constraints` 与
`image-disk-not-found`。本地比较确认 dtc 的 DTB→DTS 输出将 `00 32 4b 00`
写成字符串 `"\02K"`，再编译时按八进制转义解析为 `02 4b 00`。
pmc8180c 的 bob/ldo10/ldo11 三个 `regulator-min-microvolt` 因此丢字节；
不能将这项构建回归归因于 SQ1 硬件差异。

`board_dts.from_dtb` 现在把属性导出为明确字节数组，并保留内存预留。
固定目录的 board.dts 已更新，新 DTB 实际编译成功；与原适配 DTB 全部属性相同，内存预留相同。
小修复包包含两套入口的 DTB、已有路由修复及对应清单；Image/模块/initramfs不变。
DTB 修复已写入 USB SSD；随后 IMG_0460 显示系统盘识别通过，旧电压约束错误未再出现。
原 v5 整盘保持原件，不冒称已重打包；新的启动阻断和处理见上节。

## SQ1 路由遗漏修复

用户确认原通用镜像可在 SQ1 启动。当前故障发生在 EFI LoadImage 前：复制的路由器
仍把 SQ1 分支指向未打包的 `EFI/SQ1Default/el2launch.efi`。
这不能推出需要另做 SQ1 内核移植，亦不能证明 6.18 载荷不兼容。
整包生成器现保留 SMBIOS 机型识别和本机 LTE 处理，将两个型号分支指向对应入口的共用载荷：
正常 `EFI/SQ2Nat/el2launch.efi`、救援 `EFI/SQ2Res/el2launch.efi`。
这里的 SQ2 目录名是历史命名，不代表伪装设备型号。

现有 v5 整盘文件保持原样，可使用 `repair-shared-image-router.py` 生成小修复包，
只覆盖 ESP 中两个 start.efi 及对应 SHA256SUMS；不格式化、不清数据、不重新写整盘。
本轮未重编内核或修改 DT/模块/固件/initramfs，未声称 SQ1 新包已经实启动通过。

这是 `100b6d75368c1bdf8a78d411d05efba0f2aa8256` 之上的整包集成。
`scripts/build.py full-image` 默认 **source**：编译 Linux 6.18.60、全部核心模块和外挂模块，
重建 DTB、initramfs，再写入原模板分区。它不是 `package` 的父包复制，也不沿用新增的 64 GiB 布局。
旧 `disk-image` 低层入口不再有隐含容量，必须显式选择模板或自定义容量。

## 固定私有输入

默认根是项目的 `private/build-inputs/`，不纳入 Git。本机绝对路径：
`C:\Users\Administrator\Documents\ChatGPT\chromebookOS\private\build-inputs`。

```text
private/build-inputs/
  template.img                       # 原 20 GiB 通用模板，不修改
  RESOURCE-MANIFEST.json
  full-image-runtime/
    INPUTS.json
    records.json                     # newc 权限、uid/gid、设备号等元数据
    files/                           # 独立运行库/固件/启动 helper/私有身份等文件
  kernel-6.18/
    source/SOURCE.json
    source/source.tar.gz             # 当前补丁后的 kernel/ 与 external/
    toolchain.tar.gz                 # ARM64 GCC/binutils，物化链接，无旧主机绝对链接
    board.dts                        # 已适配板级 DT 的显式源码输入
    firmware/
    bootloader/BOOTAA64.EFI
    EFI/SQ2Nat/                       # 签名/未签名启动组件，及可显式选用的预编译模式
```

当前运行层来自 v21-release-4k 的一次性拆分导入，包含已整合的 OMX CSD、桥接兼容和 DPU 修复。
后续源码整包构建读取独立文件，不再读取父 `initramfs.cpio.gz`，也不从父包拿 Image 或模块。
这不表示私有用户态库都已开源或全部从源码重编；私有身份仍保留，不得发布镜像/输入目录。

首次导入使用 `build.py import-private --resources EXTRACTED_RESOURCES`。
已导入目录拒绝覆盖；更新依赖时显式准备新输入根，保留原件。
原历史 20261107 源归档不能直接冒充当前运行栈：用 `prepare-full-image-sources.py`
在副本加入当前 SoundWire、Venus OMX/flush/capture/END/缓冲生命周期及 release 清理，
产出固定目录中的 source archive。输入候选3源码是该准备器的明确参数，不通过版本号猜测。
准备失败不发布 SOURCE.json；`--resume-after-layout` 仅用于已完成 capture-layout 后的中断恢复。
源码准备可在 Linux 文件系统工作目录执行，避免 NTFS 上数万小文件。

`prepare-full-image-toolchain.py --input TOOLCHAIN_ROOT --output FRESH_TOOLCHAIN_ARCHIVE`
会物化编译器/binutils链接；仅允许省略缺失的目标运行库开发链接和文档链接。
主机构建工具仍需 make、gcc、flex、bison、bc、OpenSSL/libelf开发文件、dtc、depmod、mtools、
dosfstools、e2fsprogs、pigz、Python 3.12；不会把主机依赖安装当作目标系统编译成功。

## 构建

在项目根目录的 Linux/WSL 环境运行：

```sh
python3 scripts/build.py full-image \
  --output /mnt/d/spx_img/NEW_BUILD \
  --build-dir /home/kanoyami/spx-full-image/NEW_KERNEL_BUILD \
  --jobs 8
```

参数/行为：
- `--kernel-mode source` 为默认；固定 source archive 解到新的 Linux 工作目录。
- 历史源码归档漏掉的 DTC include-prefix 相对链接由解包步骤按本源码树重建，不连接旧主机目录。
- Image、所有 runtime `.ko` 副本来自本次编译；任一缺失直接失败，不回填旧模块。
- 完整外挂模块组按 `configs/kernel-external-modules.json` 编译，统一 depmod；不允许 core-only 作为整包输入。
- `board.dts` 通过 dtc 重建，保留既有板级/EL2/音频/显示适配，而不是换成未适配的上游 DTB。
- 签名 TCB/固件加载器仍来自私有依赖；不伪造签名、不改发布 pins。
- `--kernel-mode prebuilt` 必须显式选择，只复用固定目录的 Image/DT/模块；manifest明确标注。
- `--private-root` 可整体重定位；`--template` 可显式选择另一普通文件模板；默认模板容量与 GPT 边界保持。
- `--cross-compile` 可显式覆盖工具链；不提供则从固定私有 archive 解包。

本机原模板为 20 GiB，userdata 约 4.15 GiB，ESP 2 GiB；不新增默认 64 GiB 或强制扩容。
复制模板的非数据/非ESP分区与主备GPT，**仅在新输出文件**中重新创建 userdata、metadata，清空 misc，
替换 ESP。旧模板及旧用户数据不改；正常与救援入口仅属于新产物，不改物理设备菜单。

单用户是 Android full user 0，禁用 headless/split system user、限制 max_users=1，
取消固定前台 user10。由 Android 正常建立 `/data/user/0/<PACKAGE>` 和 `/data/data/<PACKAGE>`，
不开放其他应用私有目录、不伪造 Linux UID、不做 `/0`→`/10` 映射，不迁移已有用户数据。

## 输出与界限

首启修正（2026-11-10）：HOME helper等待device_provisioned和user_setup_complete均为1，
不在向导临时HOME时退出；LoginLauncher改为Nexus后明确启动HOME，保留其他自选桌面。
有界等待窗口改为一小时，避免用户尚在向导中时旧330秒超时。
首次开机待Android工具可用后，对已发现的通用镜像磁盘在线扩展末尾userdata及ext4，
成功后写ESP完成标记，之后才判断内存并启用swap。低于12GiB仍为6GiB swap；
较大内存也扩容、只跳过swap。扩容失败不创建swap，不格式化或移动其他分区。
`full-image`的主机依赖增加aarch64-linux-gnu-gcc及静态libc，用于编译单分区长度更新工具。
新的自动首启及真实扩容尚未经设备启动验收；此前手动切换HOME已由用户确认桌面恢复。

2026-11-10本轮完成整包为 `D:/spx_img/template-source-618-20261110-v6`，使用HEAD8f5d964
及当前工作区集成，32任务从源码完成内核/全部模块，沿用原20GiB模板。
raw为21474836480字节，gzip为8295869453字节；默认壁纸、最新累加IPTSd、SPI-HID/HOME、
SQ1/SQ2分型和user0已在本次构建路径合入。未运行整盘校验、备份、回滚或设备部署。
首次旧single-user紧邻判断失败后，仅恢复未完成的disk构建；源码内核和runtime成果复用。
下面旧v5/单独启动包记录只作历史，最新完整交付使用v6。

默认壁纸使用固定输入 `private/build-inputs/wallpaper/DefaultWallpaperOverlayMica.apk`。
其中原有三个 `default_wallpaper.png`（nodpi/sw600dp/sw720dp）直接换成用户的
`C:/Users/Administrator/Pictures/bg.png` 原字节，固定图片名为 `default_wallpaper.png`。
保留原资源表/Manifest/资源名，仅重新签署修改后的资源包；不增加壁纸服务、属性或定时设置脚本。
`full-image` 在原 init 中直接替换只读 `system_ext/overlay/DefaultWallpaperOverlayMica.apk`，
不修改已有 `/data` 壁纸及用户选择。未提供此可选私有输入时仍使用模板默认图。
本轮更新启动包为 `D:/spx_img/wallpaper-default-20261110/initramfs.cpio.gz`，277368722字节；
已确认三份图均为原3276×2012 PNG，Android真实idmap保持同一default_wallpaper映射。
此包尚未写设备；旧v5完整磁盘仍未重建。图片原件、其他运行组件及上一轮SSH清理保持。

输出：`kernel-export/`、`paired/EFI/SQ2Nat/`、`disk/*.img`、`disk/*.img.gz`、
`BUILD-COMMANDS.json`、`FULL-IMAGE.json` 和逐步骤 stdout/stderr。
默认沿用用户“只构建产物”：不运行测试套件、文件系统读回、整包解压校验、整盘SHA或回滚。
编译错误、必要格式/ABI/配对约束仍会中止；启动内容哈希检查和通用整包构建的哈希对比已取消。
失败不能标作成功，旧失败日志保留；新编译候选不自动提升为已验收发布基线。
没有设备写入/重启；SQ1、新盘启动、user0应用兼容与长期性能仍未验收。
# 2026-11-10 源码显示配对修正

原模板模式同时恢复SQ1/SQ2分型：从模板ESP的`EFI/SQ1EL2/android-el2.dtb`读取SQ1的
数值GPU/GMU OPP，应用于6.18 DT副本。SQ1为661MHz/416，SQ2保留700MHz/432；不是
将700MHz简单限制到661却保留错误corner。`full_image_models.py`保留现有两菜单和机型检测，
两型号共用Image/initramfs，只选择独立DT常量；不会用旧6.3.8整张DT替换6.18适配。

`full-image` 在解包固定源码后调用 `full_image_display.prepare`，把既有 catalog-backed DPU caps、
真实 release fence、硬件过滤的 UBWC 和 BT.601/709 limited 色彩适配应用到外部 MSM 源码副本，
然后正常编译。Nov7 归档没有这些适配；单凭 `current_release_patches` 标签或相同 vermagic 不足以配对
当前 QTI HWC。输入 archive 和已验收发布 pins 不改；不复用父包模块，也不添加内容哈希启动/构建门槛。
本次已实际重编 MSM/MODPOST，通过编译不是整包/硬件/GPU 恢复验收；旧 v5 整盘尚未重打包。

## 2026-11-10：最新 SPI-HID / user0 HOME 集成与退出临时网络诊断

本地最新提交 `6b790ba406c0504cda996dd09c37be4b83aa0d90` 已将 SPI-HID 输入恢复补丁
接入 `build-lts.py` 的外挂模块源码编译：复位成功后才启用 IRQ，错误输入头触发既有恢复 work。
通用整包走同一入口，不以旧模块替代源码编译结果；不等于真实 suspend/resume 已验收。

`full_image_home.py` 在 Android 启动完成后运行有界一次性 HOME 修复，仅在 full user0 且
当前 HOME 仍是 `LoginLauncherActivity` 时调用本提交的 `apply-sq2-user0-home.sh`。
其他自选桌面保持；不禁用整个账号包、不清用户数据。此逻辑也安装到本次设备启动包。

`pack-spx-normal-start.py` 撤销本轮临时早期 SSH 的精确 init 变换并真正移除四个临时文件，
保留当前 DPU 模块、user0、原有系统启动后 SSH 及身份。它不会把旧 initramfs 整体覆盖回来，
也不会通过空 marker 假装禁用。启动内容哈希门槛不恢复，正常重启自然清除 RAM Wi-Fi 接管。
本次修复包 `D:/spx_img/touch-home-normal-start-20261110/initramfs.cpio.gz` 为 249578106 字节。
原 v5 完整磁盘镜像不因此自动更新；需要新整盘时必须重新运行完整构建。

