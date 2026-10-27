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
