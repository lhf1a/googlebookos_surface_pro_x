# 累加触摸修复与默认构建（2026-11-10）

## 行为

静置、滑动和15点采集都保留已经应用的噪声与点触规则，只检测残留。
`NoiseRuntime::tick` 不再因 `learn/slide/taplearn` 旁路旧规则，噪声候选始终
以 active 为合并基础。固定热图修复、触点状态一致性、坐标曲线和面积限制保持。

`TapRuntime` 分离实际输出 tracker 与残留观察 tracker。学习期间旧的、身份和
硬件布局匹配的点触规则仍作用于输出，新观察只形成候选，不直接改变输出。
`tapfinish` 保留旧策略；有足够残留证据时才有界扩展，仍限制6帧/60ms并要求
逐帧真实信号支持。不吞真实无接触间隙，不放宽多指/掌压/弱信号/身份约束。
没有新证据不清零。用户复测确认后保存累计结果，拒绝只撤销本轮预览。

## 本机数据与发布数据分离

用户在应用中完成新一轮采集并要求持久化。采集及复测各15/15，每个目标1组
点击、同时峰值1、取消0，均完整且无截断。复测对应独立 `USER_ACCEPTED` 文件；
摘要中的 `operator_confirmed=false` 是采样时快照，不改写该原始字段。
本轮最终 `.tap` 为0帧：没有凭空制造额外续接。拇指复测10轮各1次，仍有2次
短接触记录；不能由这些记录声称全部硬件问题或长期/跨设备手势验收完成。

本机 `.noise`、`.tap`、`.profile` 经各原生校验器校验，保存文件与运行规则一致，
仅留在设备身份绑定的私有存储中；不嵌入通用包，不提交坐标、设备身份或原始采集。
相关证据只在 ignored `out/touch-cumulative-20261110/`。

## 默认构建

标准发布 pins 更新为 `v21-release-4k-touch-cumulative`，仍使用 Linux
6.18.60-sq2-android1 / EFI/SQ2Nat。标准包只改变 `bin/iptsd`：

- IPTSd SHA256：`e531874e289686b22f10c737343128ebee9eeb5ac56cb952b88fa49a07571839`
- 标准 initramfs：`b66a8b2fdf1a39ffdb7b99715633359292207565204e38b19f8fcf017bdd5d6e`
- 本机单用户包 initramfs：`10cacda8ac97acd0d458d92965ac3d443de1c22f3a7f7425878d0e198522e2cd`

本机单用户包与标准包的父包不同，分别更新，不互相覆盖内核或用户策略。
标准包另外1270条有效记录、本机包另外1273条有效记录及其metadata保持；
所有原始压缩成员与最后私有身份成员保持原字节。恢复入口、GRUB默认选择、
音视频、网络和存储策略不变。

默认 `scripts/build.py verify/package` 使用新的 pins 与私有包；当前本地完整镜像
私有输入 `full-image-runtime/files/bin/iptsd` 及其metadata也同步更新。
历史 `private/build-inputs/touch` 是旧的非源码匹配缓存，未伪造其manifest或当作
本轮构建成果。要重建完整 touch bundle，须用当前源码和原固定哈希上游源码按
`scripts/build-unified-touch.py` 真正构建，不复用该旧缓存。

`scripts/pack-touch-runtime.py` 是可复跑的离线单文件更新器：要求父包和已验证
daemon显式SHA，保留metadata与身份尾成员，拒绝错误输入、重复更新和非新输出。
它不部署、不重启、不读取设备用户数据。

## 验证边界

同输入实际C++运行时 ASan/UBSan：原版旁路旧规则、空学习清零，exit1；
新版保留旧规则、空学习保持2帧、残留候选有界扩展为4帧但不立即应用，exit0；
源码副本回滚恢复原版逐字输出、exit1和全部原hash。持久包副本回滚恢复原父包，
重新应用得到完全相同的新包hash。相关24项主机回归和标准 verify/package通过。

正常重启后boot为 `a01507e5-8c79-4127-a6e5-25d53983f7c6`，实际运行exe哈希匹配，
`/bin/iptsd`无绑定挂载；noise/tap/坐标保存文件均原hash且自动加载逐字一致，
Android完成启动。首次BusyBox请求未改变boot ID，改用Android正常重启接口后
才通过；轮询总用时包含前一次无效请求，不作为启动耗时。重启后未新增手动15点复测。
既有视频验收沿用原发布记录，本次不新增视频、PM、长时或整体整栈完成声明。
