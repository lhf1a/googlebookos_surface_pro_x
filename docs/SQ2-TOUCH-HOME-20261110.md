# SQ2：触摸恢复与默认 Launcher（2026-11-10）

## 范围与当前结果

对象为现有内部 SQ2Nat、完整单用户0、headless关闭的系统，不是并行开发的通用整盘镜像。
本次只提交触摸恢复构建集成、Launcher维护脚本、测试和说明；不提交启动包、凭据、私有库或原始日志。
设备已部署触摸候选并正常重启；Nexus桌面/打开应用已获用户确认，HOME设置在该次重启后仍保持。
**真实触摸驱动休眠/唤醒尚未验收，不能称为完整PM修复。**

## 触摸：补回缺失的V2输入恢复

实读运行模块SHA256为`048782059cfbc7f01c27f0bd6569d17e6567278544aa390963e30dec170908b0`，
resident build-id与Nov7外部源码构建一致。该源码已有V1 PM和响应缓冲路由修复，却缺少已有V2补丁：

- `spi_hid_ll_open`：复位/供电尚未完成就启用IRQ。
- `spi_hid_input_header_complete`：非法报头计入错误但不调度既有`error_work`。

故障现场出现全零报头、SPI未就绪、iptsd退出；原watcher在恢复失败后不再处理同一计数。
后来另一次唤醒又恢复，不能描述为永久坏死，也未排除快速再次入睡的额外影响。

在副本应用已入库的`patch-sq2-spi-pm.py`，仅补齐上述两处，不改校准、阈值、原响应路由或PM策略。
`build-lts.py compile`现在于复制的SPI-HID组clean/build前执行该幂等补丁；未知源码失败即停，
原输入归档不变。既有补丁和实际函数回归脚本无需重复新增。

实际源码ASan/UBSan验证：同一必需恢复契约BASELINE退出1、MODIFIED退出0、ROLLBACK退出1；
旧版/回滚版在过早IRQ断言失败。另有旧/新行为断言、非法报头恢复、错误路径及响应路由/长度检查。
匹配6.18模块MODPOST成功，保留原有ignored-qualifiers编译警告。

部署模块SHA256：`a8504f49166b2c2d82e4a430b1cff23b431ed81e551b61fbd6125eb81b402a67`。
initramfs：`98509b49a1a40a27886971764ff52f2f5c852f618cd806234919c514ca6c1d0b`
→ `945520fe0aea15e31f0ba71e1efda74b5e104264a21cb0654e0aacc19d0d58fb`。
有效记录只改变`spi-hid.ko`；单用户策略保持。正常重启实读模块及build-id匹配，不热卸载提供者。

用户报告按电源键前后触摸正常，采到亮屏后的真实DOWN/UP与坐标事件。但内核suspend成功0、失败9，
SPI resume_count仍0；`aa00000.venus-sq2`挂起超时`-110`，Venus记录`start:hfi-resume`失败、
sys_error1、实例/缓冲0。因此只能验收此次亮灭屏交互，不能把PM entry/exit当成功休眠。
后续须解决Venus阻断，再做真实SPI唤醒验收；未禁用休眠、强制复位固件或改写其他驱动。

## 默认 Launcher：仅调整用户0的HOME入口

原实际HOME为`com.google.android.desktop.identity.login/.LoginLauncherActivity`，priority4。
仅设置Nexus HOME角色不足以覆盖此高优先级入口。因此：

1. 只禁用用户0的`LoginLauncherActivity`组件，不禁用整个包或账户服务。
2. 将用户0 HOME设为`com.google.android.apps.nexuslauncher/.NexusLauncherActivity`。

提交的`scripts/apply-sq2-user0-home.sh`逐字保留已经执行成功的维护命令。
从设备initramfs root SSH shell运行，使用`nsenter -t 1 -m -r`进入Android；脚本要求
headless=false且当前用户为0，所有命令有15秒时限并传播错误。无需再在已修设备上重复执行。
这不是自动开机钩子，也不声称已集成到尚未验收的通用镜像首次启动流程。

`scripts/rollback-sq2-user0-home.sh`仅用于还原本次已知基线：组件default-state、HOME回到原LoginLauncher。
它不是任意用户自定义桌面的通用备份/还原器；不恢复此前清空的数据，也不创建user10。
多条命令不具备原子性，失败时应检查实际组件/角色，不能假定已全部应用。

实机BASELINE/MODIFIED/ROLLBACK/重新应用分别解析到LoginLauncher/Nexus/LoginLauncher/Nexus。
Android已将组件状态和HOME角色写入用户0配置；触摸候选正常重启后再次确认Nexus默认HOME。
本次新增主机shell fixture复跑同一序列及前置条件/错误传播，不将fixture冒充新的实机操作。

## 记录与回滚范围

沿用ignored `out/sq2-user10-recovery-20261110`四角色：
`MODIFIED_FILE.cpio.gz`、`DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`。
本次触摸DIFF/ROLLBACK以立即前一单用户包98509b49…为父版本；精确patch重建和独立副本回滚哈希通过。
回滚不会恢复旧headless策略；未在设备上回滚旧驱动。历史角色与源码/测试证据保存在其`touch-resume-v2`子目录。
Launcher补充角色为HOME_BASELINE/MODIFIED/DIFF和HOME_APPLY/ROLLBACK；不替代上述四角色。
临时采集与上传文件已取回并清理。发布pins、音视频/图形载荷及全部full_*完成标志未提升。
