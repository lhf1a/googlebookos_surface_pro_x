# 构建与部署

脚本来自迭代实验室，依赖缓存和分阶段产物；全新克隆不能直接生成完整可启动 SSD。不要删除哈希或磁盘身份保护以绕过缺失输入。

## 环境与输入

Windows PowerShell + WSL Ubuntu 24.04（root），ARM64 GNU 交叉工具链、QEMU user/system、Python 3、Meson/Ninja、cpio/gzip、Git、Android NDK r28c。部分脚本含工作站路径及固定输出目录，迁移需显式调整。

| WSL 缓存 | 用途 |
| --- | --- |
| /root/.cache/sq2-probe-build/kernel | 内核源码及补丁 |
| /root/.cache/sq2-probe-build/kernel-android-out | 当前配置、模块、gen_init_cpio |
| /root/.cache/sq2-probe-build/googlebook | 系统及 donor 解包/挂载 |
| /root/.cache/sq2-graphics/src/mesa-26.2.4 | Mesa |
| /root/.cache/sq2-graphics/build-gles | GLES 构建 |
| /root/.cache/sq2-graphics/android-ndk-r28c | Android 工具链 |

来源见 configs/sources.lock.json、graphics-sources.lock.json、probe-inputs.sha256。操作者自行取得合法镜像/固件。

当前模块构建使用的完整配置快照保存在 configs/sq2-internal-kernel.config，包含内置 NVMe 和模块化 FUSE。它记录当前构建目录状态（含后续 PM 模块配置），不是重新构建相同 Image 哈希的充分条件；仍需匹配源码、补丁及工具链。

build-test3-ssh.sh 要求 SQ2_AUTHORIZED_KEY_FILE 指向自己的公钥；主机身份及 known_hosts 留在忽略的 private/sq2-ssh/。Windows SSH 安装脚本要求 -PublicKeyPath，离线 MSI 自行下载，见 scripts/SSH-OFFLINE-README.txt。

## 构建链

1. 恢复镜像：fetch-googlebook.py → inspect-recovery.py / extract-googlebook-stage.py，验证固定哈希。
2. 历史基础：build-probe.sh、build-test2-kernel.sh、build-android-test-kernel.sh。**最后一个是旧 USB profile，会重设配置并要求 NVMe 关闭，不能直接重建当前内置 SSD 内核。** 当前配置保留 CONFIG_BLK_DEV_NVME=y、SoC、Binder、memfd、FUSE、文件系统能力。
3. 图形：build-sq2-mesa.sh 应用 gbos-vm mapper5 与多平面补丁；build-sq2-minigbm-bridge.sh、build-googlebook-graphics-overlay.py 准备 overlay；读回测试在 probe/graphics/。
4. 触摸：build-test3-iptsd.py → build-touch-fixup.sh；检查最后 cpio 成员中的有效 IPTSd/配置。
5. 连接：fetch-connectivity-reference.py、fetch-wifi-hal-source.py 等准备 donor/headers；build-sq2-connectivity.sh 编译桥接/HAL；不能任意更换 donor HAL 版本。
6. 音频：build-sq2-audio-tools.sh、patch-sq2-audio.py、patch-sq2-audio-hal.py 及 policy/路由，保留和校验原始 HAL。
7. 电源/输入：build-sq2-lid-power.sh 合并现有配置并编译模块，保留 NVMe；SPI PM 为未完成实验，用户暂停真实睡眠测试。
8. SSD：build-googlebook-internal-ssd.py 准备布局；pack-googlebook-internal-ssd.sh 合并旧基线/触摸/当前 addon。旧归档有固定输入哈希，缺少时仍需复原前置阶段；并非从源一键构建。

## 增量打包及安装

当前只关屏内核由 build-sq2-screen-only.sh 在已有内置 NVMe 配置上增量构建 Image，不更改配置或模块 ABI。patch-sq2-screen-only.py 添加启动后只读的系统休眠屏障；smoke-sq2-screen-only.py 用 QEMU 启动实际 Image 验证拒绝 freeze/mem/disk。GRUB 同时传入 suspend.screen_only=1 和 nohibernate。新内核需要与包含新版 lab-keep-awake 的 addon 一起部署。

pack-sq2-power-connectivity.sh 在已有缓存下打包 /init、蓝牙/输入脚本、PM 模块和 HAL；当前 /init 包含 FUSE 加载。输出是 addon，不能独立启动。

当前完整 initramfs 为已验证的图形/触摸/音频基线加 addon。不要向任意旧归档反复追加。scripts/audit-sq2-initramfs.py 扫描全部 gzip/cpio 成员，以最后条目为准；gzip -t 检查压缩。重建需核对有效 /init、libEGL、IPTSd、音频/蓝牙 HAL 和内核 ABI。

Install-GooglebookInternalSSD.ps1 是有身份保护的特定实验盘安装器，仅用于授权整盘安装。修复存储挂载/HAL 不需格式化 userdata。旧 USB/外置 SSD 脚本属于不同阶段，不可混用。

在线更新先核对盘和 ESP，备份原 initramfs，写同 ESP 临时文件，检查 hash/gzip，sync 后原子替换，保留正常启动、独立救援和关机项。正常入口在 Android 启动后自动开启公钥 SSH，不执行诊断 Wi-Fi 连接；sq2.ssh=0 可单次禁用。需要完整启动日志或救援联网时使用救援或 sq2.debug=1。

## 验证

```sh
python3 -m unittest discover -s tests -v
python3 -m compileall -q scripts tests
bash -n scripts/pack-sq2-power-connectivity.sh
sh -n probe/test2/init-googlebook-native
```

PowerShell 脚本通过 parser 检查；设备验证另见交接文档，不把语法/QEMU 检查等同于硬件通过。
