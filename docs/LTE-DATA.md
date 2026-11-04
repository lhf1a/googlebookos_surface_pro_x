> 2026-11-04：本文为历史记录。当前构建、自动本机 FSG 与产品清理见 [LTE-PRODUCT](LTE-PRODUCT.md)；旧实验工具已退出产品源码路径。

# SQ2 LTE 数据接入（2026-11-03）

本节更新此前“FTM/RF 未就绪、LTE 尚不可用”的历史记录。原机 FSG、IPA、QMAPv4/RMNET
已经完成真实数据与 Android NetworkAgent 接入，重启和默认部署结果见 HANDOFF 最新节。
本节记录 LTE03 的单卡、私有 APN IPv4 数据基线。后续原生 Radio、SIM 设置和状态栏
集成见 [LTE-RADIO.md](LTE-RADIO.md)；LTE03 保留为独立回退，电话、短信未实现。

## 已取得的区分证据

恢复用户的 666e56f 私有检查点后，QMI EFS21 的实际 handler 读取上限确定为 3072，
不是 IDL 数组的 32768。修正后响应从 Malformed 变为协议成功但 fs_errno=1/EPERM，
RF 路径不在其 ACL 中，停止该读取路线。

完整 Android 新启动时先准备 AP DIAG DATA2/DCI4，再发布 CTRL0，原机固件完成
3 字节 feature mask 协商，接受 raw 请求并返回 non-HDLC 帧；AP 会话退出后 CMD 仍可读。
原单位 FSG bd124b2d… 与零 FS1/FS2 的对照显示：NV453 自然为 0，rfnv 有 14 个条目，
DMS interfaces 为 UMTS/LTE（5,8），标准 online 成功，NAS 注册且 PS attached。
此前 own-NV 为 FTM=1、rfnv 空、interfaces=0。没有清 FTM、移植 SQ1 NV 或手填 RF selector。
这证明原 FSG 在本轮有效，不声称唯一解释全部历史失败。

NV1878 inactive、空 /rf、PDC platform selected error16 在有效 LTE 下仍可出现，
不能再用这些状态认定“校准缺失”，也不要为修饰状态加载/选择新的 MCFG。

## 数据通路

- 保留原 MPSS、adopted remoteproc 与 RMTFS `-r` RAM shadow；本机 FSG 每次启动复制到
  `/run/rmtfs/modem_fsg`，不写物理 NV 分区。seed 校验单机磁盘序列、ESP marker、FSG 哈希。
- 使用已验证 IPA4.1 模块 9b6e6dd6…、原固件 510f1002…、DT 49228267…；原 stock RMNET
  模块 0aaaf85f… 对应 6.3.8-sq2-android1，完整 modpost 和运行内核 65 个导出符号比对通过。
- DPM embedded endpoint4/1，RX2、TX11（与 IPA sysfs TX/RX 方向相反）；先开 DPM，再 WDA。
  WDA rawIP2、QMAPv4 UL/DL8，WDS mux1、IPv4。所有请求沿用同一个服务 socket/client。
- RMNET child 使用 mux1/flags15/mask63，MTU1496；不盲目提高已验证的 parent MTU1500。
  WDS 返回地址/DNS/MTU，地址以 /32 配置；Android netd 根据 LinkProperties 安装分网络路由与 DNS。
- Native session 持有自己的 WDS handle；TERM、父进程死亡或断连时释放自己的调用和 DPM。
  忽略 SIGPIPE，避免 Java Process.destroy 关闭输出管道时打断 native 清理。
  Android 服务轮询已有 airplane_mode_on/mobile_data，每秒两次本地 Binder 读取，无诊断网络探测。
  数据关闭撤下 agent 与 bearer；飞行模式下请求 low-power1，恢复请求 online0。
  mode ACK 后仅回读最多 5 秒等待异步切换，拒绝 FTM2/reset 等模式。
- 数据建立失败按 5–300 秒退避；不会重启 remoteproc/IPA。接口残留地址只在独占启动时清理。
  Android 进程意外退出最多恢复三次，不会产生无限进程崩溃循环。
- NetworkAgent 发布真实 CELLULAR、INTERNET、metered、NOT_VCN_MANAGED，未写 VALIDATED。
  系统按现有策略标记 VALIDATED；原实验室 skip-google-setup 设置 captive_portal_mode=0，
  因而这个标记不能证明 Google 连通性探测通过。真实联网证据是独立 HTTPS 和 Chrome，
  本次未修改该全局策略。Wi-Fi 优先，关闭 Wi-Fi 后自动回退 LTE。没有虚构订阅或免费流量声明。
  未接入 NAS 漫游状态，因此不宣称 NOT_ROAMING，Android dump 可显示 ROAMING；这不是实际漫游判定。

## 实机验证与边界

指定 LTE 源地址的 ping 3/3、运营商 DNS、HTTPS200 通过。随后 Network.openConnection
通过系统分网络 DNS/路由得到 HTTPS200。数据关闭/开启完成撤下与重连；飞行模式设置对应
只读 DMS mode1→mode0，随后恢复数据（首次 WDS14 尚未就绪时，退避后成功）。
该测试只修改并恢复全局飞行模式设置，没有广播关闭 Wi-Fi/蓝牙，不能冒充完整飞行模式 UI 回归。

已关闭 Wi-Fi 做真实应用回退：默认网络转为 LTE，原 Chrome 加载新的 HTTPS 百度页面，
rmnet 接收从约 193 KB 增至 2.10 MB；Wi-Fi 恢复后重新成为默认。独立 40 秒恢复计时器
和 EXIT trap 均保留。自建测试 APK 被系统安装验证拒绝，没有绕过验证或修改安全设置；
最终证据使用已有 Chrome。截图/原始网络、SIM 与设备信息留在忽略的 out/private。

尚未验证 IPv6、移动热点、换卡/APN 自动识别、漫游策略、长期移动切站/耗电或电话/SMS。
正常启动不运行测试、DIAG、Terminal UI、KVM witness 或连续网络诊断。

## 可复用源码与构建

`probe/lte/` 包含 QMI 数据/模式 helper、RMNET netlink 创建器、Java NetworkAgent 服务、
单机 seed 与异步启动助手。`stubs/` 仅供 javac 编译隐藏 API，绝不能打进 dex；运行使用设备
真实 framework。设备当前 Android17 签名已反射核对，SDK35 仅供编译公开类型。

运行 `bash scripts/build-sq2-lte-data.sh`，需要既有 SDK35/android-15 build tools、JDK、
gcc、aarch64-linux-gnu-gcc、qemu-aarch64。构建执行 native/ARM64 的报文长度、重复 TLV、
结果/事务号、APN 与断连解析 selftest；Java IPv4 边界 selftest 在设备执行。
`build-sq2-rmnet.sh SOURCE MATCHING_BUILD NEW_OUTPUT` 要求完整 Module.symvers，不修改原配置。
IPA 继续使用已有 `patches/sq2-ipa-v4.1-lab.patch` 和其构建流程。

`pack-sq2-lte-data.py BASE BUNDLE OUTPUT --unit-serial SERIAL --fsg-sha256 HASH --apn APN`
只接受基线 init957，bundle 包含 RUNTIME 七项以及本机 modem_fsg.bin。打包器固定已验证
模块/固件，生成运行哈希清单，审计 21 个新增/变更的有效条目；其余有效文件逐字节相同，
密码 SSH member 重新置于最后。产物含本机私有 FSG 和登录资料，禁止入 Git 或公开发布。
标准公开构建不带该单机数据。`sq2.lte=0` 跳过本套启动，Mesa fallback 保持原 payload。

最终已验证载荷：initramfs `783cdacf105c3a4c6f2cf57774fc507dbcd2dc6823f0a32403b6ca3d2d4dca52`，
EFI `a41e26f66ceb7e66b194b50ce39176ca68675c77f8ceda7d30bbd823075a2aaf`，DT仍49228267…。
默认GRUB `883cd0a5d44754a9a3a45f2009b741c72a8d2b93e6d2ef08eb947a273b54e079` 直接chainload
`/EFI/SQ2LTE/lte03-once.efi`（文件名保留构建名，但默认菜单不读取once env），原Mesa第二项。
设备 `/EFI/SQ2LTE/lte03-SHA256SUMS` 验证该EFI、initlte03、IPA DT和原Image；原两个payload
各自SHA256SUMS仍全通过。回到无LTE原默认只需恢复已校验9f1ee01e…原菜单，不必覆盖固件/镜像。
