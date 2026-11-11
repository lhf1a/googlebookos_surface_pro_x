# KGSL/SDE Round6 暂停保存与产物包

用户先暂停目标，后授权“提交并推送当前修改，构建产物打包”。本次只保存既有结果，不恢复开发、不重新编译、不连接或重启设备。旧文档中的 active/继续指令不覆盖本条。

## 保存内容

- KGSL59缺失OPP带宽诊断：返回与策略不变；完整严格60对象/3模块已构建。
- 既有匹配6.18 MST内核、SDE20模块；启动源v7含真正绑定门禁、严格参数、module-prelude依赖/身份/锁及一次有界SSH诊断。
- 64项相关主机回归、两例真实匹配内核完整init QEMU回放；后者为synthetic EROFS，真实helper在错误/缺批准时停止，不是Android或硬件显示验收。
- 主链原版/回滚prepare23仍HANDOFF0；修改版停止handoff，fixture观察等待后143。实际tar解包→rollback→精确reapply（含权限）已验证。
- generic-PHY backend是独立未接线WIP，不当成SDE native运行成功。

## 私有本地产物包

`out/packages/kgsl-sde-round6-20261111-host-only-<commit>.tar.gz`，实际提交号、每项SHA256和大小见包内`MANIFEST.json`与同目录SHA256侧车。

包含匹配Image/配置/符号、规范化497模块与117固件、v7启动源、既有HOST-ONLY完整initramfs、相关四角色及验证证据。全包校验每项数据，不重新编译。二进制与私有证据不推Git，不把旧EFI诊断目录装成新部署包。

**PRIVATE / HOST-ONLY / NOT-ACTIVATABLE**：initramfs继承开发运行时SSH凭据和固件，仅供本人本机保存，不上传公开Release或发送他人。

HOST initramfs：263155021字节，SHA256 `a1af0fff688b2730a141a035074c1e86cb6c09ce88e58f86701a06f15f1d9c16`。独立解包1295项（990 regular、2设备节点、0符号链接）与打包树严格一致；相对历史runtime恰24项变化，历史每项uid/gid保留。两次staging/runtime owner断言错误及修正证据保留，不计产品失败。

## 未完成及恢复边界

- 最终外层权限确认和独立payload audit在暂停时未收敛，保留初审，不宣称全部完成。
- `bin/prepare-native-gpu`打包mode0777来自Windows源副本，需恢复后审查/规范化；保存阶段不修改此产物或隐瞒该位点。
- 十档GPU OPP均保留；用户暂不制定590/661/700MHz实验带宽政策。
- SDE root/catalog/eDP/PHY/AUX/clockICC/PM、真实提交/fence/显示/性能/SQ1-SQ2未验收；禁止生成批准或激活候选。
- `.122`最新只读SSH超时；先前boot/timer状态不是本次重新确认。没有新设备写入、信号或重启。
- 只有用户明确恢复才接续开发。公开checkpoint：`configs/kgsl-sde-checkpoint-round6-20261111.json`；完整本地状态：`out/kgsl-resume-20261111/CURRENT.json`。
- 原有六个rescue-console未跟踪文件是独立未提交工作，仍保留，不纳入本次KGSL/SDE提交。
