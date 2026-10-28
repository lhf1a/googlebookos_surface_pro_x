# Chrome vkCreateDevice：性能计数器错误分支审计

2026-10-28，离线检查 combined-v5；本审计未访问设备、未执行 GPU、没有宣称修复。

## 已确认控制流

实际 Chrome 的 Scudo `invalid chunk state when deallocating` 栈落在 donor `vkCreateDevice` 的 `0xd241c`。该位置是 `free` 调用，而不是 shader 编译或 native-buffer 释放。

固定对象：`vulkan-v77-bridge.so` SHA256 `e18b5adf7d213c2058c3ef93028ed6db1af8aa83e398f07681545bff9382dd54`。

1. `0xd1488` 调用对象构造函数 `0x15d7a0`，返回值存入 x22。构造函数 `0x15d828` 从参数块复制 physical pointer 和 GSL table pointer 到对象 `+120/+128`。
2. GSL table 为 physical object `+536`。初始化器 `0x125c1c` 读取 GOT `0x2e29e0`，`0x125c30` 写入 physical `+808`，即 table `+272`。NDK `llvm-readelf -rW` 解码 Android packed relocations，明确 GOT 符号为 **gsl_perfcounter_select**。普通 GNU readelf 的简略结果容易漏掉这个 packed relocation。
3. `0xd2328` 的 `calloc(1,8)` 返回 x26，`0xd233c` 的 `malloc(24)` 返回 x23。x26 的两个32位字段为27和0；x23首字段持有x26。这两次分配均在循环之前。
4. `0xd238c` 从对象 `+776` 的数组取当前记录，循环上限为对象 `+784`。`0xd23b8` 经上述 table `+272` 调用 gsl_perfcounter_select，参数来自记录 `+144/+148`；若记录 `+152 != -1`，还会在 `0xd2408` 调用一次。
5. 任一调用返回非零则进入 `0xd2410`。默认 allocator 分支在 `0xd241c` 释放 x26、`0xd242c` 释放 x23，随后 **`0xd2434` 返回 `0xd23d0` 的循环递增**。两指针没有清零或重新分配。下一记录若同样失败，会再次释放同一对象。

这构成明确的重复释放控制流，解释了多 queue/record 与仅一个 queue 探针之间的差异，但还需要实际 Chrome queue 参数及调用返回值证明本机触发的完整路径。Scudo 栈本身不提供首次释放和所有权历史；不要把推断写成已经完成动态跟踪。

`gsl_perfcounter_select` 在配套 libgsl 中为 `0xcb840`，最终经平台函数表 `+264` 调用。当前审计尚未证明实际平台 endpoint 的地址/错误码，因此不能将其直接称为权限错误、DRM不支持或某个内核 ioctl 故障。也不能把可选 app-profile 跳过等同于此计数器分支被禁用。

## 下一步及约束

- 捕获 Chrome 真正的 VkDeviceCreateInfo：queue family/count/flags/priority，extension names，legacy features，已知 pNext payload及未知sType。
- 在私有系统 loader 探针复现至少两个**实际支持的**队列/队列族，只创建和销毁设备。保留单队列对照；不要伪造超出公布数量的请求。
- 需要时在 donor GOT 的 gsl_perfcounter_select 边界做有限跟踪，记录记录ID/返回值，保持真实调用结果；仅日志不改变功能。
- 候选修复需要明确成功/失败混合路径的所有权。不能简单删掉 free、返回虚假成功或只给 Chrome 永久禁用 Vulkan。特别是成功注册一个记录后，其他记录失败时，原分配可能已被成功路径引用。

## 有界诊断头文件

`probe/graphics/quest2-vk-create-trace.h` 提供 `sq2_trace_create_device(info,allocator)`，供根 agent 通过单独编译开关加入私有 trace HAL。需要 `-llog`，logcat tag为 `SQ2VkCreate`。记录上限为16个 queue info、每个32个priority、64个extension名字（各255字符）、每条pNext链32节点。只读取有明确编译期结构的 feature 字段，未知pNext仅记录sType；不输出指针、不改参数。

NDK Android35 clang `-std=c11 -O2 -Wall -Wextra -Werror` 独立编译烟雾检查通过。实际 Chrome 日志采集及相关候选构建由主 agent 负责；编译通过不是动态验证。

原始反汇编只保留在忽略目录 `out/quest2-gpu/kmsg/vk-*-audit.dis` 与 `vk-object-init.dis`。

## 最小创建对照

`scripts/build-quest2-vulkan-device.sh` 构建 `quest2-vulkan-device.c`，输出 `out/quest2-gpu/vulkan-device/` 与独立清单。`SQ2_VK_DEVICE_QUEUES=1` 为基线，`=2` 为双队列；按实际公布的数量先在一个支持 graphics/compute/transfer 的 family 请求最多两个，不够则使用第二个 family，priority均0.5。可用队列不足返回77，不算通过。两种模式须分进程串行执行。

探针仅使用明确的系统libvulkan，启用API1.1、零instance/device extensions、零features、空pNext和默认allocator。打印真实family属性与所有创建参数，只创建/销毁instance/device，无队列提交/图像/窗口。驱动内部初始化仍可能访问GPU，因此仍需普通设备保护和串行规则。构建已通过，实机结果由主agent补充；它不是Chrome完整参数重放。

## 主代理实际设备复现

同一combined-v5和原系统libvulkan，公布family0有4个队列。单队列创建/销毁PASS；双队列（同family、优先级0.5、零扩展/features/pNext、自带默认allocator）在create尚未返回时SIGABRT134。新tombstone为Scudo invalid chunk state，仍在donor0xd241c，和Chrome相同。未提交任何工作，正常Mesa哈希、boot ID与SF/system_server PID保持。原始证据vk-device-one/two/two-crash.txt在out，不能计为修复。

进一步静态检查：d23a4把此前成功状态w28保存到w20；失败释放后d2430恢复该状态。若之前某项成功，最终d295c非零分支会在d296c/d2970/d2974把已释放x23接入对象列表。这意味着修复还必须覆盖成功/失败混合路径，不能简单清零指针或删除free造成泄漏。当前尚未修改donor，仍需完整清理/回调所有权审计与候选控制流验证。
