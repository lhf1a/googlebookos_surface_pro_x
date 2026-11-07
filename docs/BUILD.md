# 唯一标准编译基线：Linux 6.18 LTS

更新：2026-11-07。当前版本 **6.18.60-sq2-android1**。6.3.8专用编译链已退休；历史记录不是可执行入口。

## 保持当前验收内容

以 `configs/kernel-baseline.json` 的文件清单为准，不重新混装旧模块。当前原件：
- Image：`db374853bb5df0cdaa5215b1226256adedda3292a93084e874438a4ba706c982`
- initramfs：`5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185`
- DTB：`c137b9604b908e39aba4be6ecd98e55cd9986fe3fd7522c93069bef7489c956b`

有效启动参数为 **Mesa、原生60Hz、EL2、unrestricted suspend、nohibernate**；GPU为当前700/RPMh432档。
保留音频、SSAM电池、SPI触摸response、Venus队列锁、IPA预留与RMNET、Android拥有网络及启动后SSH等当前修正。
不因为旧文档默认高通/661而改变当前验收内容。单机用户确认与六帧视频证明不等于跨设备或长期认证。
Scene跨用户链接属于已授权设备数据状态，不复制到其他设备；Scene GPU读数缺失仍是已知问题。

## 一个入口，明确区分核验、组包与编译

在具备Python3的主机运行；源码编译需要Linux/WSL与匹配ARM64工具链：

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/6.18-release-FRESH
python3 scripts/build.py compile --help
```

默认输入为 `private/build-inputs/kernel-6.18/EFI/SQ2L618C`。`package`逐字节复制当前EFI包并核对清单，
**不是源码编译，也不是生成新的完整磁盘镜像**。旧磁盘重组入口已撤销，避免悄悄装回6.3.8。
`build-surface-pro-x.py`、`build-source-boot.py`、`build-source-disk.py`、`build-sq2-default.py`兼容文件名均转到同一CLI，旧参数明确拒绝。

`compile`使用显式source/config/build-dir/output/cross-compile；实际执行Image、modules、dtbs与模块导出。
默认配置为 `configs/kernel-6.18-baseline.config`。完整基线还需要匹配的外挂源目录与模块清单，具体参数以`compile --help`为准。
新编译输出与验收发布包分离，未经配套打包/验收不得覆盖发布pins。不能以仅核心编译通过代替全部外挂模块与initramfs功能对齐。

## 本地输入与恢复

- `private/build-inputs/kernel-6.18/`：当前私有EFI包、源码归档与构建清单；不纳入Git。
- `out/mainline-618-20261107/`：改动前Git bundle、未提交diff、未跟踪文件归档和本轮输出。
- `out/kernel-baseline-20261107/retired-638/`：退休工具逐字节备份。
- `configs/retired-kernel-tools.json`：退休清单与原hash。仅退休旧工具，不删除真机回退入口。
- 既有四角色与验证记录继续保存在 `out/kernel-6.18-migration-20261105/`；新记录只追加。

## 隐私与验收边界

当前私有包保留原SSH身份，不能当成匿名公共镜像发布。Git仅保存源码、配置、清单、测试与文档。
不把IP、序列号、用户编号或原盘UUID作为编译/运行准入。SQ1/SQ2通用交付目标保持，但本轮基线是当前SQ2已观察产物，不能假称6.18双机新镜像全验收。
本轮主线整理不写设备、不重启，不清除userdata或已有恢复能力。
通用开发与身份分离规则见 [DEVELOPMENT.md](DEVELOPMENT.md)。

## 已执行的完整源码编译形式

解开私有源码归档得到`kernel/`和`external/`后：

```sh
python3 scripts/build.py compile --source SOURCE_ROOT/kernel \
  --config configs/kernel-6.18-baseline.config --build-dir FRESH_BUILD \
  --output FRESH_EXPORT --cross-compile TOOLCHAIN/bin/aarch64-linux-gnu- \
  --external-sources SOURCE_ROOT/external --jobs 8
```

显式`--resume-build`只复用config与ABI匹配的构建目录。缺少外挂源时默认拒绝；`--core-only`必须明确指定且产物标记不完整。
本次完整入口28步exit0、15个外挂模块覆盖成功；发布包另经verify保持原hash。源码导出不等于新boot包已验收。


## 2026-11-07：独立 Qualcomm 全图形调试包（只在 6.18 上适配）

用户明确要求在已经适配的 6.18 上替换高通图形，不允许在 6.3.8 上演进。
本分支复用当前 6.18 Image、DTB、模块和已验收配套；旧版本仅作为历史用户态移植资料，
不恢复退休的 6.3.8 编译链，不改现存设备回退。当前默认 Mesa 发布 pins 保持不变。

私有重建链为 restore-sq2-qualcomm-graphics-runtime.py → prepare-quest2-cpu-access.py →
prepare-quest2-618-layout.py → pack-sq2-qualcomm-full.py → prepare-sq2-618-debug-entry.py。
各入口参数以源码 --help 为准；输入是固定来源的私有 donor 与当前 6.18 父包，不含单机准入。
最终专用 packer 只接受已经真机验证的 15 库 layout8/bank15 runtime manifest，拒绝旧 14 库
和旧强制 4 通道中间包；这个 pin 是私有二进制来源/配对完整性，不是硬件身份白名单。

6.18 上修正两点：mapper3 私有 CPU 访问桥在真实 mmap/DMA_BUF 同步后上传 GPU-only AHB；
GLES 的 CPU 纹理通道从旧强制 4 恢复为实际能力字段，保留 bank15。未修改当前内核、DT、GPU档位。
真实隔离命名空间测试包括 165 次上传、多尺寸、内纹理上传到外 AHB 的独立读回、HWUI 三帧及
GLES/Vulkan 探针；通过不代表完整桌面冷启动/游戏/跨设备验收。

新包只加入 opt-in qualcomm-full-start 和配套 runtime，原父包所有非 init 文件逐字节保持，
原凭据 addon 仍最后生效。EFI 构建只新增 SQ2QDbg 独立目录与第三项，原两项内容/顺序/default 保持。
本地产物带实验室原 SSH 身份，不能匿名公开分发。

当前缓存策略以 2026-11-07 AGENTS.md / DEVELOPMENT.md PORT-07 最新规则为准：**先推进启动测试，仅在启动后遇相关问题时按需清理 Chrome 的已确认图形缓存，直接删除，不备份缓存**。
只动态发现 Chrome 用户/资料路径；保留账号、Cookie、历史、密码、凭据、下载、设置、普通网页缓存及其他应用全部数据。
Mesa↔Qualcomm 双向切换及普通启动均不自动或强制清理，不设预启动清理/回执门槛；按需处理前停止 Chrome，完成后重启 Chrome。原驱动、6.18 Mesa 默认与启动回退仍保留。
历史4752d501…调试包要求旧全应用回执，文档修改不会自动移除旧包中的门槛。旧全应用控制器已停用；
当前试启入口应取消全应用及 Chrome-only 预启动清理/回执要求；不得用伪造完成回执绕过旧门槛，不沿用全应用扫描。
本次规范修改未执行真机缓存删除、未部署新图形包、未重启。

本轮 private 候选/字面验证/缓存来源证据保存在 out/qualcomm-graphics-618-20261107，
既有四角色继续位于 out/kernel-6.18-migration-20261105；新增原生 payload/menu/cache 角色是补充而非替代。
