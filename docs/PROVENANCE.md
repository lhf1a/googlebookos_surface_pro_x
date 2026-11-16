# 当前 Hybrid 运行库来源（2026-11-16）

统一获取清单：`configs/hybrid-dependencies.json`；Windows 准备入口及使用说明见 [DEPENDENCIES](DEPENDENCIES.md)。

| 类别 | 固定来源 | 分工 |
|---|---|---|
| Android / AOSP | mica 16471258、Cuttlefish 16373615、AOSP HIDL android-16.0.0_r1、frameworks/av e2f098935447ca4945946de5cb69db843fe3f003 | 系统分区、软件安全 HAL、源码 HIDL 与 Stagefright/P010 |
| Quest 2 v77 donor | build 51584880050400150；第三方 OTA 地址和 hash 见 quest2-full-donor-candidate.json | gralloc/metadata、libgsl、原生 QTI Vulkan/QGL 等闭源核心；Hybrid 不采用其 EGL/GLES |
| QTI CAF + AOSP 源码 | SM8350 display e9bbe9a980d408af36b52f2abaae7c35647b7425；SM8250 media 05f3cd8dbfa66f2e79ffda3cb294697374510160；mapper5 AOSP 接口 | 按当前平台 ABI 构建 HWC/SDM、mapper/allocator、OMX，不是直接复制 donor 同名服务 |
| Mesa / libdrm | 26.2.4 / 2.4.133，NDK r28c；graphics-sources.lock.json | EGL/GLES、Turnip 与 DRM |
| 本仓源码 | probe/ + scripts/ | Vulkan 路由、gralloc/layout/backlight 与 dma_heap/ION 内存适配 |

Quest 归档可按站点公布 SHA 校验；历史 vendor hash 验证不代表完整 ZIP 已验证，更不代表 Meta 签名链认证。Surface 音频/SLPI 等 OEM 固件是共用硬件输入，与上述 Android 运行库来源分开。
已完成组件编译、整盘装配和匹配 Venus 增量构建；具体产物与验证范围见 [HYBRID-SLEEP-20261116](HYBRID-SLEEP-20261116.md)。原配置中的历史 candidate 状态保留，主机装配不提升真机验收。

公开的内核重建输入见 `configs/hybrid-kernel-source.json`。外部归档包含源码及三个与相邻 DTS 对应的 DT overlay，不含 OEM 固件、ELF、驱动二进制或凭据；内容范围见 [归档说明](../source-dependencies/kernel-6.18/README.md)。这份明确审核的源码/DT 归档是下方历史“不发布第三方源码归档”规则的限定例外；原版权和许可保留，KernelSU LICENSE 位于内核补丁中。

---
## 早期来源记录（历史，不是当前 6.18 构建入口）

# 来源与发布范围

固定来源见 configs/sources.lock.json、configs/graphics-sources.lock.json、configs/probe-inputs.sha256 及脚本断言。哈希表明输入一致，不等于验证 Google 发布签名。

| 组件 | 来源 / 版本 |
| --- | --- |
| linux-surface kernel | spx/v6.3，b86f786594dc9e53c57fcfa2aa58ada9541506bb，加本地补丁 |
| qzed ARM64 配置 | 7281316889197641763c29242c0120de70c3db11 |
| gbos-vm 参考 | 3b08ed29a73858644c65f24e06ee2a7cfb7e3774 |
| Googlebook 恢复镜像 | mica 16471258 |
| Cuttlefish donor | aosp_cf_arm64_only_phone-userdebug 16373615 |
| Mesa / NDK | 26.2.4 / r28c |
| minigbm | 455b7456397dff01443584b09e2fd594079e905c |
| IPTSd | 3.1.1，加触点状态补丁 |

Adreno-Tools-Drivers 为构建参考，固定版本见 graphics lock。其 KGSL 路线不能直接替代 MSM 接口；未盲用 A7xx/A8xx 补丁，Turnip 未验证。

不发布恢复镜像、专有固件、驱动二进制、整盘镜像、第三方源码归档、SSH 身份/公钥、Wi-Fi 凭据及原始日志；构建者自行获取输入并遵守条款。

上游组件和派生代码保留各自版权/许可。仓库没有以新的统一许可证覆盖所有第三方或派生内容；再分发二进制前需按具体组件履行许可义务并核查输入许可。
