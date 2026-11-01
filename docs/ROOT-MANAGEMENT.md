# KernelSU Next / Zygisk / Device Faker

2026-10-31：用户授权在新指定的 Surface Pro X 实验设备上安装应用和重启，
目标为 QQ、微信与手机共存。SSH root 不等于 Android 应用的 KSU 授权。
使用当前 6.3.8 内核源码集成，不迁移 GKI，不安装手机预编译内核。

## 当前候选

Next legacy HEAD b933b1f7 的模块编译因 preempt_enable_no_resched_notrace
在 Linux 6.3 MODULE 下不可用而失败；内建还发现 VFS/SELinux API 缺口，未部署。
选取 v3.2.0-legacy / 9b08e88862000d5c50fb2e43a5b75123cf472e54 作实验基线。
报告版本 33129 来自该提交 git count 2979 加上游公式 30000+150。
patch-sq2-ksunext-63.py 补齐政策复制函数声明及跨编译单元的 policy 布局宏；
尚需真实 Android 策略加载验证，不把编译通过视作 root 管理可用。

prepare-sq2-ksunext.py 解包已固定哈希的原 surface kernel，复用 memfd/蓝牙/
音频/只关屏补丁。所有实验在 /root/.cache/sq2-ksunext，正常缓存不替换。
build-sq2-ksunext.sh 产出 out/ksunext/Image-candidate，禁止直接当发布包使用。
当前候选 SHA256 074cf3057a471b090a2dcdddced2b5db6612e22cc0391b499e59da933898d52b。
与目标设备有效配置比较仅新增 CONFIG_KSU 与 CONFIG_KSU_KPROBES_HOOK。

smoke-sq2-ksunext.py 使用实际 Image 做 QEMU 检查。未传 lsm=selinux 的首次
运行在早期 KSU 域查询中 panic；与真实入口一致地传入 lsm=selinux enforcing=0
后通过只读 guard 和 freeze/mem/disk 拒绝检查。该依赖必须保留，不掩盖失败。
没有进行任何实际休眠测试。

新目标是原 sq1-latest65-661 默认入口所在设备，不是之前 SQ2 的磁盘布局。
设备盘序列/容量、ESP marker、ESP/data UUID、root backing source 均已核对。
独立 /boot/ksunext-trial/Image；只追加对应的一次性 GRUB 项。
原菜单备份 EFI/BOOT/grub.cfg.before-ksunext，原默认入口/DTB/initramfs不变。
正式菜单 ID 为 `ksunext-trial`，环境文件 `EFI/BOOT/ksunext-once.env`。
清除一次标记成功才选择试验入口；未提供硬件挂死时的自动复位保证。
两次真实启动已通过：先验证候选内核，后验证模块开机启动。最终 boot
02478599-130f-46e6-a286-d4002adbcca3，Android completed=1，guard Y。
用户确认桌面和触控正常，已将 `ksunext-trial` 设为默认，原默认入口及救援保留。
菜单 SHA256 205bde363af271911a46ae4cb1677e7e5787e266a88fb379543819b5c4e6d9ad；
提升默认未再次重启，不打断用户登录测试。私有日志、菜单和部署脚本只在 out。

## 实际安装与验证

官方 Next 3.2.0 管理器安装于 user10，包名 com.rifsxd.ksunext；签名大小0x3e6、
哈希79e590…与内核固定值一致。ksud实际查询版本33129。
APK SHA256 96c2bbbf1b973461fe82dd1ed17f89deb86a6a5a9d7c4cf079bd32091131ef57。
ksud SHA256 bb231b0f77d6ee78a4362ff751b3cc54c934293a4b0acde177afa59c4a64da00。
Zygisk Next 1.5.2 官方包SHA ce50b13fecb9e4456db69ef17b715f7cfbc094004dd9f30f7d6348dfab619a96，
通过ksud模块安装及SELinux规则检查；重启后模块描述报告zygote/root成功。
Device Faker v1.6.0官方包SHA 0d7b717ba25d541b3ae064b93ffd618b330af32f1f55c9abebfaf8a560f9be12，
重启后modules_info确认64位device_faker已载入，两个模块update=false。
无需改原initramfs：现有Next init.rc注入在真实Android上已触发模块生命周期。

独立临时应用local.surface.identityprobe在真实zygote子进程验证：
- 无模板：Build及SystemProperties均Surface Pro X/Microsoft、desktop。
- Samsung模板：Build及SystemProperties型号SM-F9560、品牌samsung，SDK仍37。
- Xiaomi模板：型号23046RP50C、品牌Xiaomi、characteristics=tablet，SDK仍37。
- 两次伪装期间外部getprop型号仍Surface Pro X；撤销临时匹配后探针恢复真实身份。
SystemProperties通过反射调用进程内属性读取，未另写native测试库。
探针已force-stop；正式config只覆盖com.tencent.mm和com.tencent.mobileqq。
目标SDK28探针首次安装被平台校验拒绝；重建target35后正常安装，没有关闭验证服务。

## 机型模板

沿用 Seyud/device_faker 原实现，不重写伪装引擎。
配置仓库固定 0a49b74539cb138e5369ea989b7be411bcbba017：
- 微信：transcend/Samsung/samsung_z_fold6.toml，SM-F9560，作者标注微信平板模式。
- QQ：transcend/Xiaomi/xiaomi_pad_6.toml，23046RP50C，作者标注 QQ 平板模式。

用户要求选模板时通知，由用户手动验证真实共存。已通知上述选择。
两个模块与上述模板均已安装，但QQ/微信实际登录共存待用户校验，不声称已通过。
安装前新设备只发现应用宝，未发现QQ/微信；用户准备自行验证。
默认采用应用进程隔离，避免全局型号、SDK 或系统 DPI 变更。

## 默认构建集成验证

2026-10-31：默认启动包加入固定版本 KSU 内核、官方 ksud/管理器和异步首启助手，
救援菜单使用独立的原始无 KSU 内核。支持 `--ksu off`；不默认安装伪装模块。
KSU 助手/菜单 3 项、显示菜单 5 项、触控构建 7 项测试通过。
完整启动包构建及 gzip 校验通过，审计确认凭据 addon 最后、载荷哈希匹配。
最终 initramfs SHA256：cbeb2bce278d25546ff45af728a293fc5f3dd0d9e6e043f9f5c2cca01d2f9eb1。
此次未生成完整磁盘镜像、未部署或重启设备。新增自动首启安装路径仅通过沙箱验证；
之前实机通过的是相同内核和官方用户态的手动安装链路。
