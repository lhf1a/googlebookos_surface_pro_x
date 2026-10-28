# Quest 2 v77 Vulkan 窗口交换链

2026-10-28。候选 `v77-vulkan-ahb-v4` 仅在独立 mount namespace 实测；正常 Mesa、启动镜像、boot ID、system_server 和 SurfaceFlinger PID 保持。不是已完成系统替换。

## 接口与所有权

当前系统 libvulkan SHA256 为 `027b70dce9e5e9188f4497e20c371fdc079124243e561b99a7a9ac8b61f69185`。其 Android native-buffer 节点为 72 字节：前 56 字节与 v77 兼容，+56 为 usage3，+64 为只在调用期间有效的 AHardwareBuffer 指针。CreateSwapchain 在 `0x31ec4..0x32018` 构造节点，后接 SwapchainImageCreateInfoANDROID；BindImageMemory2 在 `0x35e98..0x35ed4` 把节点加到原链前，lazy bind `0x33954` 也保留 BindImageMemorySwapchainInfoKHR。不能只复制旧 56 字节或原地修改 loader 的结构。

`quest2-vulkan-wrapper.c` 复制完整节点，只替换 handle。`quest2-v77-ahb.cpp` 通过真实 mapper5 import 持有原分配，再生成独立的 124 字节 QTI 视图和 dup 像素 FD；不从新节点尾部借用 AHB 生命周期。两种注册表合用 512 MiB 上限，raw native 注册表最多 128 项。兼容视图不传给真实 mapper5，mapper getter 经 resolver 返回已导入的原 handle。

仅支持已核实的非 protected、linear、单层单平面 RGBA8、2D、单 mip/sample。native 必须位于链头；普通 KHR 别名、plane-binding、重复绑定等未覆盖路径明确拒绝。当前 loader 的 create marker 和 native 后的 bind-swapchain tail 保留；固定 v77 对后者按未知节点跳过，不进入其内部 tag2 派生路径。

进入 donor 前完成整个 batch 的 prepare；进入 donor 后即使 batch 返回失败也保留所有已提交租约，避免部分绑定后的悬空指针。销毁时先检查固定 donor BuildID、四处指令、入口地址、image/memory vtable、唯一引用和对应 raw view；原 DestroyImage 返回后才释放 mapper import/FD。无法证明唯一所有权时保留租约并增加 sticky failure，不提前释放。BuildID 通过运行时 PT_NOTE 查找，因为 patchelf 会移动原注记位置。详细原库析构证据见 [生命周期审计](QUEST2-VULKAN-AHB-LIFETIME.md)。

## 实机结果

探针 `quest2-vulkan-window` 使用原系统 libvulkan、真实 ImageReader/ANativeWindow 和 FIFO swapchain；逐帧 acquire、提交非均匀 RGBA 网格、present，再由消费者等待 native fence 并按真实 stride 检查全部像素。

| 测试 | 结果 |
| --- | --- |
| 64×64 create-only，8 张图像，1 周期 | 通过；0 submit/present |
| 64×64，150 周期 × 12 帧 | 1800 帧、1800 native fence 全部通过 |
| 13×7、257×129、1920×1280、2880×1920，各 3 周期 × 12 帧 | 每种 36 帧全部像素一致 |
| 已有 compute/image/raster + AHB attachment-grid 3 周期回归 | 通过 |
| 每周期 native 图像销毁与最终设备销毁 | native_views/native_bytes/native_failures 均归零；实际唯一 owner 检查通过 |

探针 default linker namespace 无法 NOLOAD 取得 sphal 中的适配器统计，明确记录 `wrapper-log-required`；没有加载第二份注册表制造零值。上述归零结论来自实际 wrapper 输出。当前 loader 在这些测试中采用 create 时导入；deferred bind 分支只完成离线审阅/mock，未声称实机覆盖。

原始日志在忽略目录 `out/quest2-gpu/v77-vulkan-ahb-v4/`；文件为 `window-create-first.txt`、`window-first.txt`、`window-64-150.txt`、`window-13x7.txt`、`window-257x129.txt`、`window-1920x1280.txt`、`window-2880x1920.txt`、`ahb-regression.txt`。完整像素检查不是可见桌面、游戏或长期系统稳定性的证明。

## 固定产物与复现

| 文件 | SHA256 |
| --- | --- |
| vulkan-v77-bridge.so | `b087293a0a59ce4d4a3ed25dfc29179a920c2fcab31587de3f8489fb481f3cbb` |
| libquest2-vulkan-wrapper.so | `1cf8564084cbce59937ba512cd58f26a44bd148c80d306336b1fcb4db1e0ebd7` |
| libquest2-v77-ahb.so | `6f7562d6f9503067f7de7eb094591145134ef43682b0bdad833e4e50baeb49a5` |
| libqti-mapper3-bridge.so | `ade19efb787641daf1305c1d5787e93cc29029fd1462074455ff3a616979ab75` |
| quest2-vulkan-window | `e2ced1eaf140da74cff6f0b27c713135a3704276ea601a1630f4ca3d2f63862e` |

其余 GSL/LLVM/utils/ION 与 v3 九库中的对应文件相同；原 donor 可执行 section 未改，无 Direct 配置。构建顺序：

```sh
bash scripts/build-quest2-v77-ahb.sh
bash scripts/build-quest2-vulkan-window.sh
python3 scripts/prepare-quest2-v77-vulkan.py --system-loader --ahb-view \
  --adapters out/quest2-gpu/v77-ahb-adapters --output out/quest2-gpu/v77-vulkan-ahb-v4
```

prepare 拒绝已有输出。同步候选、窗口探针和 `VULKAN_WINDOW_SHA256SUMS` 至设备私有目录 `v77-vulkan-ahb-v4/` 与 `vulkan-window/`；另同步新版 namespace helper、系统探针清单和 runner。先从 retained root 执行：

```sh
SQ2_V77_AHB_VIEW=1 SQ2_V77_AHB_REVISION=4 \
  sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --vulkan-window-create
SQ2_V77_AHB_VIEW=1 SQ2_V77_AHB_REVISION=4 \
  sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --vulkan-window
```

`SQ2_VULKAN_WINDOW_WIDTH/HEIGHT/FRAMES/CYCLES` 默认 64/64/12/1。保持外部超时和串行 GPU 测试，检查完整日志与 wrapper 计数，不只检查退出码。后续仍需 AHB 采样/共享/导出、其他格式、同一进程 GLES/Vulkan 公共依赖组合、真实 SurfaceFlinger、应用及持久化重启验证。
