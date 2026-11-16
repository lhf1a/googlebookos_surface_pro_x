# 2026-11-16：主线休眠修复接入 hybrid 整盘镜像

主分支已快进到 `619af8b9fbc13a267add557f7875026a62e7bd27`，包含休眠默认修复 `940a769`。
原工作区的唯一 hybrid、SQ1/SQ2 共用、Windows 依赖准备和匿名镜像修改保留；
`full_image.py` 的三个冲突按“休眠安装 + 原匿名清理/复用入口”合并，没有二选一丢失功能。
WeChat 子模块已切换主线固定 `4839c1b`；本轮没有另行生成 WeChat 模块。

## 本轮实际构建

- 使用上一轮 `6.18.60-sq2-android1` 的真实 KOUT。Image、kernel.config、Module.symvers、System.map 逐项匹配后，只复制并构建 Venus 外部模块源。
- 应用 transport 保留与 PC_PREP/PC_READY 握手四文件增量。
- 新 `venus-core.ko` SHA256：`043ba0ae4c176db04c94fd32f9a06b596ee98a1a173832fb13d7c052440285f8`。
- `venus-dec.ko`、`venus-enc.ko` 虽随同编译，结果与旧模块逐字相等。其余 staged 模块不重编。
- 内核、摄像头、传感器、Mesa、HWC/OMX、图形 glue、触控向导及扩容 helper 均复用已有完成产物。
- 运行时对比：只修改 `init` 和两份 `venus-core.ko`，新增 `surface-sleep-defaults` 和 `s2idle-desktop-services.jar`，没有删除其他项。
- 实际 JAR 的 `DesktopPowerService$screenStateReceiver$1::onReceive` 等待值解码为 1000 ms；启动 bind/helper 各一次。既有用户自定义设置保持逻辑不变。

## 新镜像

目录：`/mnt/spx-workspace/chromebookOS/out/hybrid-sleep-20261116/image/disk/`。

| 产物 | 字节数 | SHA256 |
| --- | --- | --- |
| `surface-pro-x-6.18-hybrid-generic.img` | 21474836480 | `087fa033f79ba450c671cf0fa66d621985d85e3b2e602f432c6ad1abc90ee025` |
| `surface-pro-x-6.18-hybrid-generic.img.gz` | 8199955842 | `ea0883c80a904058d8d90d422f7bad51361533804ad617376e7842d6513a0f00` |

最终 initramfs SHA256：`e4f246066cc70a1e997f55f935782361b705970f41657433c1ca891275bb57ab`。
GPT、FAT/ESP 全文件回读、最终 ESP、gzip 全流大小/哈希和清空身份分区检查通过；
正常/救援入口的 SQ1/SQ2 DT、Image、EFI 与前一镜像逐字一致。
root 锁定、默认 SSH 禁用、无继承 Wi-Fi 配置，保留首次设置。

## 可重复增量入口

```sh
python3 scripts/rebuild-hybrid-venus.py \
  --reuse-components PREVIOUS_REUSE.json --kernel-build MATCHED_KOUT \
  --source PREVIOUS_VENUS_SOURCE --output FRESH_VENUS_OUTPUT
python3 scripts/build.py --reuse-components FRESH_VENUS_OUTPUT/REUSE.json \
  --private-root PREPARED_INPUTS --output FRESH_IMAGE_OUTPUT
```

匹配检查失败会在编译前拒绝，不能只靠相同 vermagic 混用。默认构建加载休眠依赖并拒绝旧 Venus 模块。
`full_image_reuse.py --assembled-runtime PREVIOUS_INITRAMFS` 可记录并复用来源未变的已编译小型 helper；
只读取明确列出的输出，不沿用旧 init 或个人状态，文件与源码摘要均检查。

## 验证范围

Venus 同命令三态构建均 exit 0，休眠模块门禁为原版 1／修改版 0／实际回滚版 1，
回滚模块与旧模块哈希相等。保留编译器既有 unused-variable 警告，没有隐藏诊断。
原 source、旧镜像和旧组件不覆盖；四角色主链继续扩展，旧角色冻结在 ignored `sleep/previous-*`。
测试中两项旧假设已纠正：主机生成镜像的哈希回读不等于启动基线门禁；真实 Mesa 测试显式取新复用清单，不从旧私有 runtime 取带旧标记的 Turnip。

本轮是主机编译、装配和回读验证，没有连设备、部署或重启。
主线已有的 SQ2 休眠验收不自动变成这份新整盘镜像的 SQ1/SQ2 真机验收；长时功耗仍未验证。
全公开输入加 Windows 导出即可从零重建所有 HWC/OMX/runtime 的缺口未在本轮扩大处理。
