# KernelSU Next / Zygisk / Device Faker

集成基于当前 6.3.8 内核源码，不迁移 GKI 或使用手机预编译内核。

## 内核集成

- 基线：`v3.2.0-legacy`，提交 `9b08e88862000d5c50fb2e43a5b75123cf472e54`，版本 `33129`。
- `patch-sq2-ksunext-63.py` 补齐策略复制函数声明及跨编译单元的 policy 布局宏。
- `prepare-sq2-ksunext.py` 在 `/root/.cache/sq2-ksunext` 解包固定哈希的原 surface kernel 并复用 memfd/蓝牙/音频/只关屏补丁。
- `build-sq2-ksunext.sh` 产出 `out/ksunext/Image-candidate`，相比有效配置仅新增 `CONFIG_KSU` 与 `CONFIG_KSU_KPROBES_HOOK`。
- `smoke-sq2-ksunext.py` 进行 QEMU 检查时必须传入 `lsm=selinux enforcing=0`。

## 默认构建与管理器

默认启动包集成固定版本 KSU 内核、官方 `ksud`/管理器与异步首启助手，救援菜单使用独立的无 KSU 内核；支持 `--ksu off` 关闭集成，默认不安装伪装模块。

- 管理器包名：`com.rifsxd.ksunext`，官方 Next 3.2.0，安装于 `user10`，签名大小 `0x3e6`。
- 现有 Next `init.rc` 注入即可触发模块生命周期，无需修改原 initramfs。

## 模块与机型模板

伪装采用应用进程隔离，避免修改全局型号、SDK 或系统 DPI：

- **Zygisk Next 1.5.2** 与 **Device Faker v1.6.0**，沿用 `Seyud/device_faker` 原实现。
- 模板配置仓库固定为 `0a49b74539cb138e5369ea989b7be411bcbba017`，正式配置仅作用于 `com.tencent.mm` 与 `com.tencent.mobileqq`：
  - 微信：`transcend/Samsung/samsung_z_fold6.toml`，型号 `SM-F9560`。
  - QQ：`transcend/Xiaomi/xiaomi_pad_6.toml`，型号 `23046RP50C`。
