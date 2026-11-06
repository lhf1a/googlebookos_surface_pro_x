## 2026-11-07：保存全量进度，6.18 LTS成为唯一编译基线，统一主线入口

按用户要求，本轮保存当前全部迁移成果并收敛统一入口。唯一版本6.18.60-sq2-android1；发布基线精确保持现机Mesa/60Hz/GPU700-RPMh432/EL2/unrestricted/nohibernate，不从旧文档切回高通或661档。

- scripts/build.py以及build-surface-pro-x/build-source-boot/build-source-disk/build-sq2-default均使用同一verify/package/compile CLI。旧磁盘重组参数拒绝，不让旧6.3.8模块混入。
- 当前发布基线package已实跑：Image db374853…、initramfs5b3a70…、DTc137…及EFI/options均逐字节匹配，未修改基线文件。package是验收包复制，不冒充源码编译或完整磁盘生成。
- 新统一compile已实际执行28条命令exit0，包括Image/modules/dtbs、modules_install、12组外挂clean/build、15个模块覆盖与depmod；depmod.stderr为空。导出843文件，完整链external_runtime_overrides_integrated=true。新源码输出不自动覆盖验收包，仍需配对initramfs/DT/EFI才能发布新的修订。
- 源码归档303774784B，SHA256 26f3045793cdf6e44379b75cb0adda0b7b32f2914537fcfb02f83c8d752a3b3b，全91832成员回读。私有路径private/build-inputs/kernel-6.18；归档恢复后12组外挂另行构建也全部通过。
- 65项6.3.8专用工具/配置/专属测试已退休，清单configs/retired-kernel-tools.json，原件逐字节备份out/kernel-baseline-20261107/retired-638。保留9个模块builder均先校验唯一6.18源/ABI；移植参考不作为旧编译入口。没有删除设备回退和旧参考缓存。
- 最终unittest类集合258项：243通过、15因本地历史输入缺失等原因跳过。原全discover混入CLI脚本、普通用户/root路径权限造成的失败日志保留，改为识别TestCase集合并在WSL root运行后通过；不是把原失败写成通过。
- 统一入口B/M/R相同--help命令exit0：原旧disk/--template参数→新verify/package/compile→独立副本恢复原输出及hash，再重新应用新入口。稳定四角色继续原路径，ROLLBACK.sh增--build-entry；新补充角色在out/mainline-618-20261107/entry-transaction。

本轮未写设备或重启。私有SSH/Wi-Fi/FSG/用户数据/Scene APK不进Git；Scene跨用户链接是设备授权状态，Root已确认，GPU应用读数适配仍未完成。用户确认其他现机功能已测，不推定SQ1/跨机新整镜像或长时认证。

备份：out/mainline-618-20261107/{repository-before.bundle,working-before.patch,untracked-before.tar}。
实编：out/mainline-618-20261107/compiled-baseline/build-audit.json；精确发布包：同目录accepted-release。
当前任务仅主线保存/构建收敛；后续继续Scene读数等明确待办，不重复已完成部署。

## 2026-11-07：Scene Root已获用户确认；GPU读数缺失定位到应用接口适配

用户确认软链接后“已正常进入 Root 模式”。只读复核scene-daemon PID9268、UID0，daemon.lock已创建；链接两侧files同inode，daemon/up.sh原hash保持，boot ID未变。此轮Scene Root入口验收通过，不扩大为所有KSU路径证明。后台仍有SN Error、部分appops/包查询错误及manifest缺失，不能写成Scene所有功能无错误。

随后用户报告收集不到GPU频率。实际/sys/class/devfreq/2c00000.gpu：cur_freq曾读405000000 Hz；在scene-daemon自身挂载命名空间中再次读177000000 Hz，max_freq=700000000 Hz，driver=adreno，governor=simple_ondemand；两个样本不能证明全部调频稳定性，但频率源存在可读。/sys/class/kgsl不存在。

已只读提取当前Scene APK。classes.dex的GPU路径选择相关方法La/qq0;->f/i/k含/sys/class/kgsl/kgsl-3d0、其devfreq及/sys/class/devfreq/gpufreq，未发现当前2c00000.gpu路径。DEX字符串/候选引用提取不是完整反编译，边界保留；结合运行态，当前明确缺Scene对MSM/adreno devfreq路径的识别。推荐修读取器按实际GPU设备动态发现cur_freq，Hz除1000000显示MHz，使用真实频率，不伪造KGSL写接口或把max_freq冒充当前频率。

没有改GPU档位/驱动/Scene APK、没有写sysfs或重启；Scene画面频率尚未修复。下一步只做读数接口适配，不重复Root软链接与已完成验收。证据在ksu-scene-20261107/{scene-root-user-confirmed.stdout,scene-gpu-frequency-baseline.stdout,scene-daemon-gpu-read.stdout,gpu-reader-strings.json,dex-gpu-refs.json}。

## 2026-11-07：按用户明确授权，Scene跨用户软链接已建立，应用复测待确认

用户明确允许跨用户共享并要求直接ln修复，覆盖此前对此次Scene全局映射的限制；仅用于当前设备，不推广为通用镜像或其他应用策略。
运行时读取当前用户10，确认源数据目录存在且目标不是已有文件/目录/链接，执行ln -s /data/user/10/com.omarea.vtools /data/data/com.omarea.vtools，exit0。目标files与源files均为66305:8013258，确认指向同一数据，未复制/迁移/清理用户数据。scene-daemon与up.sh哈希未变，未改KSU授权、内核、EFI或重启。

真实路径对比：修改前stat目标files失败exit1，修改后成功且inode相同。本机独立副本B/M/R：路径不存在exit1→读出pristine app data exit0→撤销后不存在exit1；源数据SHA始终相同，重新应用fixture。稳定ROLLBACK.sh增加--scene-link LINK EXPECTED_TARGET，只有精确匹配的软链接可被删除，不递归删除或处理源目录。补充MODIFIED_LINK.json与LINK.patch在ksu-scene-20261107/link-transaction，重建校验相同。

最新读取仍只有Scene前台进程，daemon未重新启动，旧失败日志未清除；已请用户在Scene重新进入Root模式。链接安装成功不等于Scene功能验收成功。boot仍1704e2b0-435e-4d27-a644-c286d7109388。下一步仅检查用户正常重试后daemon PID、锁文件和Scene root状态，勿重复ln或重复其他功能测试。

## 2026-11-07：Scene root弹窗追踪到后台进程使用错误用户数据路径

实读Scene daemon.log与daemon.stderr.log均为：open lock file failed: open /data/data/com.omarea.vtools/files/daemon.lock: no such file or directory。当前Android用户10，Scene只在用户10安装（N1 2026.10 Alpha14），实际files目录为/data/user/10/com.omarea.vtools/files；用户0未安装。init和Scene自身挂载命名空间中原硬编码路径均无法访问，真实目录存在且inode相同。因此不能简单推荐删除su的-M参数，也不要创建全局跨用户软链、迁移用户或放宽授权。

up.sh启动后台后无条件echo Scene-Daemon OK!，未核验后台PID，与截图PID为空相符。独立同UID/SELinux域/挂载命名空间检查中，direct、direct-M、fork、fork-M均得到uid=0/gid=0/context=u:r:ksu:s0，exit0。原最小fork探针仍ENOENT/exit71；增加exec前printf的检查可通过，说明该探针差异仍需单独解释，不能声称KSU所有路径无缺陷。原结果不删除，也不把原探针失败当成Scene弹窗唯一根因。

已确认的应用层阻断为Scene后台锁文件路径。优先解决方案：Scene应用/daemon统一使用运行时应用filesDir（或可配置的传入数据目录），避免固定/data/data或用户编号。若闭源版无可配置入口，可另做只作用于该daemon的私有mount namespace兼容映射：运行时发现所属用户目录，严格隔离挂载传播并保留原入口，禁止全局跨用户映射。本轮只诊断，未部署该兼容层、未启动Scene后台、未改变应用数据/授权/内核/启动包，未重启。需真正看到daemon PID并由Scene确认root后才算修复。

证据：out/kernel-6.18-migration-20261105/ksu-scene-20261107/{scene-daemon-error.stdout,scene-path-layout.stdout,scene-mount-comparison.stdout,uid-su-matrix.stdout,original-probe-repeat-and-scene-files.stdout,desktop-error.png}。daemon原件SHA256 2c9fe2d0d0e60e0a59a5b44a7b2f95597c3cb7de0432cb35694718071bdafa09已保存在私有out，仅供对比；静态扫描未检出明文路径，不据此猜测可用参数或修改二进制。

## 2026-11-07：用户确认其余功能已测，当前聚焦 KSU/Scene，保留桌面报错现场

用户报告“除ksu外都测试完成”，记为当前设备用户验收反馈，不再把这些功能列为用户尚未测试；不据此虚构逐项采集数据、SQ1跨设备或长期认证。KSU授权Scene但Scene报无root，随后用户报告桌面错误并要求保持现场。

只读确认：当前boot仍1704e2b0-435e-4d27-a644-c286d7109388，Android框架PID3650/3144未变。KSU33129/ksud3.2.0，su_compat=1；Scene实际UID1010269授权已加载、线程均marked。Scene日志setWorkingMode(root) Fail。以运行时读取的Scene UID/SELinux上下文和挂载命名空间执行独立探针：stat/access虚拟su成功；fork后exec /system/bin/su返回ENOENT，uid/euid仍为应用UID，probe exit71。此探针不带Scene自身seccomp过滤，不能替代应用内最终验收；根因尚未确定，不改授权、不伪造su、不扩大放权。

上传的独立探针仅在/data/local/tmp/sq2-uid-su-check-20261107；没有改内核、启动包、Scene数据或应用授权。没有重启或重启桌面。采集crash缓冲区发现gscd因/dev/tpm0不存在反复SIGABRT，但尚未证明这就是用户看到的桌面提示，待截图/原文确认。不为消除日志而禁用安全服务或伪造TPM。

证据在out/kernel-6.18-migration-20261105/ksu-scene-20261107，四稳定角色保持；本轮未生成KSU修正候选，旧IPA/Venus B/M/R不能充当KSU修复证明。下一步保持现场，对照用户错误原文及实际su执行链定位，不重复测试已经用户确认的其他功能。

## 2026-11-06：6.18 视频/LTE 配对部署后新启动通过，用户确认触摸正常

此记录取代下方“视频候选未部署/未重启”的当前状态；下方保留历史证据。
用户恢复适配并允许重启后，已部署同一匹配 Venus 队列锁 initramfs 5b3a70c3…，
配合新增 IPA 设备树 c137b960…，仅请求一次 Android 正常重启。
请求时间 2026-11-06T15:46:39Z；SSH 请求等待超时124不是启动失败。
用户随后确认“已进桌面，触摸正常”；通过 EasyTier 动态发现 spx-sq2 新地址后，
实读新 boot ID 1704e2b0-435e-4d27-a644-c286d7109388，6.18.60-sq2-android1，
sys.boot_completed=1、bootanim stopped。没有再次部署或重启。

LTE 对比：用户先前手动开关确已 mobile_data=1、airplane_mode_on=0；旧6.18缺少 IPA
设备节点和 0x9b000000/0x60000 staging 预留，helper 在 /proc/iomem 安全核对处退出，
不能误判为用户未开LTE或校准失效。新增 scripts/prepare-sq2-lts-ipa-dtb.py：仅从正确
板级LTE参考DT取IPA硬件描述，动态解析并重绑定provider phandle，补no-map和reserve-map。
保留全部已有属性、6.18 QMP资源范围和VIDEOCC；不复制单机FSG、身份或凭据，不绕过内存检查。
不能在已作为System RAM的旧运行态热加载，故和视频模块配套进入正常重启。

部署：initramfs SHA256 5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185，
DTB SHA256 c137b9604b908e39aba4be6ecd98e55cd9986fe3fd7522c93069bef7489c956b。
写前重新辨识同盘ESP和Android根源，完整上传回读与激活后回读均一致。
当前ESP两个文件均留 .before-lte-video-20261106 备份，分别39fe0b5a…/0db117f6…；
GRUB、6.3.8回退和其他载荷未变。空间整理只移除已有主机完整校验备份的更旧7b375e…包。
未切图形后端、未改GPU档位、用户数据/凭据/Wi-Fi选择保留。仍是私有实验室包，不可匿名发布。

新启动实测：
- 触摸启动check-device=0、IPTSD_RUNNING、ready；bus_error_count为1(-22)，两次采集未增加。
  用户确认桌面触摸正常，这次已覆盖自动启动，不等于跨设备或长期验收。
- 同一6.3.8基线High+B Surface/CSD六帧向量、同一探针：硬件sent7/frames6/eos1/failed0，
  exit0；六帧全部与既有独立FFmpeg NV12参考逐字节一致。旧失败为sent1/frames0/exit5。
  boot ID与system_server/SurfaceFlinger PID全程不变；既有scratch帧已恢复。
  这证明该AVC向量恢复，不等于所有格式/分辨率、编码、长播放都通过。
- IPA setup completed successfully；staging显示reserved，rmnet_data0创建且Radio data-ready。
  LTE已连接，Android原生CELLULAR NetworkAgent带VALIDATED；绑定rmnet_data0对1.1.1.1
  ping3次收2次、exit0，RX包0→2/TX34→37。实际蜂窝收发已证，首包丢失原因未定，
  不将3次样本写成吞吐或稳定性通过；未关闭Wi-Fi、未启动第二bearer owner。
- 实体Surface-Pro-X声卡、AUDIO_READY、电池100% Full/外接电源online保持。
  这是驱动状态，不替代本次听感或麦克风录音验证。

四角色继续使用out/kernel-6.18-migration-20261105根目录的MODIFIED_FILE.dtb、DIFF_FILE.patch、
VERIFICATION.txt、ROLLBACK.sh。IPA B/M/R：原件FDT_ERR_NOTFOUND exit1；修改件compatible
为qcom,sc8180x-ipa exit0；独立副本回滚后同原错误exit1且hash完全相同。
Venus实际队列函数B/M/R保持-22→0→-22，src/dst绑定独立实例ctx_q_lock；回滚源hash一致。
没有为了证明回滚而撤回正在工作的设备。

证据：out/kernel-6.18-migration-20261105/lte-video-paired-20261106/{deployment,post-boot-validation}。
完整基线对齐目标仍active：麦克风、蓝牙、USB、Terminal实用路径、休眠及SQ1跨机回归等仍需
6.18实际证据；GPU频率例外保留。不重复运行已完成的部署/重启驱动，下一步从剩余功能差异继续。

## 2026-11-06：真实视频帧验证定位VB2缺锁；配对修正包已生成，未部署/重启

上一目标轮是有效进展：触摸热修正用户确认恢复，39fe0b5a…已固化；本轮继续完整6.18
基线对齐，未缩小目标。仍在boot ac554f76-29d6-4699-90e6-3c7079e40847、Android1，
system_server3237/SurfaceFlinger2775保持。GPU频率例外不动，未切换图形后端。

不能把video0/Codec2 running当作视频通过：复用基线e3f88650…NDK探针，核对内嵌
ce117e50…H264 High+B（I B B P B P）六帧向量；通过ELF映射恢复相同压缩流并由FFmpeg
独立生成691200B NV12参考。硬件Surface/CSD首帧输入后OUTPUT_ERROR=-10000，
sent1/frames0/eos0/exit5；内核明确在vb2_core_queue_init:2661触发无queue lock/wait_prepare
的WARN并返回EINVAL，调用链m2m_queue_init→vdec_open。不是发现SMMU或固件错误后猜参数。
同探针/同流的软件对照sent7/frames6/eos1/exit0，六帧与独立参考全部逐字节一致。
原有scratch帧已移回原路径；没有重启media/Android、没有重新加载PAS/Venus/provider。

直接对比6.18内核源：原生Venus已给src/dst队列共用独立inst->ctx_q_lock并在open初始化；
迁移的6.3.8定制Venus缺这些接口。新增scripts/prepare-sq2-lts-venus-queue-lock.py，
在独立副本补同样的ctx_q_lock字段、decoder/encoder两队列绑定与初始化/销毁。
保留原inst->lock以免与既有固件状态锁递归；不通过空等待回调绕过6.18检查。
结构布局改变，core/decoder/encoder整套重编；禁止只热替换decoder或热重启固件核心。

匹配6.18-cgroup/GCC13.3构建exit0，原代码未改。实际队列函数夹具：
BASELINE queue_init=-22、无锁；MODIFIED queue_init=0、两队列同锁、跨实例锁不同；
ROLLBACK恢复-22，decoder/encoder均一致，检查命令均exit0。最初encoder夹具因提取过宽和
缺少stub字段编译exit1，已限至实际函数并补齐字段，重跑通过；原错误证据保留。
这些只证明接口拒绝点修正，不宣称硬件解码恢复或并发/长播放通过。

新私有配对initramfs：204679294 bytes，SHA256
5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185。
只变5处有效Venus模块副本和etc/sq2-video-native/SHA256SUMS；所有副本匹配同套构建，
depmod无错误，配对EFI校验通过。触摸04878205…、音频/SSAM/凭据/DT/固件/早期网络策略
逐条保持；二进制patch重建一致，独立副本rollback恢复39fe0b5a…原hash。
当前仍未部署此包，未重启，已询问用户正常重启时机；不要重新构建或重复试旧包。

本轮补充状态：电池现在内核100% Full与Android100%/Full一致（先前98/100是旧观测，
不能据此判定持续读数偏差）；health未知仍未伪造。LTE Radio进程存在，但仅wlan0/tun0/
lo/ip6tnl0，无已观测蜂窝网口，联网未验收。/dev/kvm与security VM crosvm存在、用户10
已启动，不等于Terminal Debian/HTTPS已通过。麦克风、蓝牙、USB、休眠、SQ1/SQ2完整
回归仍需6.18自己的证据，不能继承6.3.8历史通过结论。

证据 out/kernel-6.18-migration-20261105/{parity-runtime-20261106,video-queue-lock-20261106,
video-queue-lock-paired-20261106}。四稳定角色未替换；新增--venus-queue-source（源副本目录）
与--venus-queue-initramfs回滚分支。下一步按CHECKPOINT继续，完整目标保持active。

## 2026-11-06：用户确认触摸恢复，同一修正已固化；本轮未重启

用户对热替换后的点击/拖动/双指验证请求回复“已恢复”。这是当前SQ2热验证通过，
不是跨设备/长时间/冷启动验收。spi_hid_input_body_complete按实际rx_buf选择response/input，
长度保护仍有效；iptsd二进制、阈值、用户校准、SPI PM逻辑未改。

已将同一模块048782059cfbc7f01c27f0bd6569d17e6567278544aa390963e30dec170908b0
固化到 /media/esp/EFI/SQ2L618C/initramfs.cpio.gz，203848874 bytes，SHA256
39fe0b5a89f85d1cb19a6f31bdd48c86554e101ac2f8d1ea94f1e136d626d234。
写前重新确认ESP与loop0根源同属当前NVMe；完整上传回读/gzip/配对SHA256SUMS通过，
激活后再次完整回读一致。唯一有效条目变化spi-hid.ko；原Image/DTB/EFI/GRUB/6.3.8回退/
其他载荷/固件未变。未切换GPU后端、未改变GPU频率、未清理应用数据。

ESP保留 initramfs.before-touch-response-20261106.cpio.gz（7b375eb5…）及原校验清单。
因空间不足，仅删除已完整回读并与主机145257f4…原件逐字节相同的更旧video-clock备份；
6.3.8回退未动。稳定ROLLBACK.sh的--touch-response-initramfs可恢复副本，已验原hash。
四角色与B/M/R结果延续：BASELINE误读input→-22/reset；MODIFIED正确response→0；
ROLLBACK恢复-22/reset，三次夹具退出0；真越界仍拒绝，正常touch不变。

本次没有重启，boot仍ac554f76-29d6-4699-90e6-3c7079e40847。
当前会话热修正和用户反馈已通过；新启动包的自动触摸初始化留待下一次获授权正常重启验证，
不得提前标记冷启动完成。证据 out/kernel-6.18-migration-20261105/touch-response-paired-20261106/deployment；
最新CHECKPOINT.json已更新。未提交/推送Git，私有包及凭据仍仅本地保留。

## 2026-11-06：已恢复推进；VIDEOCC 已启动，触摸响应缓冲区修正已热载入

此条覆盖下方历史暂停保存点。用户已明确恢复部署开发，当前又授权“可以热验证”。
EasyTier 节点名 spx-sq2 对应10.126.126.3；当前工作站是10.126.126.5，不能把此前对
.5的连接拒绝解释为SQ2启动失败。SSH继续校验已有主机身份，IP不是产品准入条件。

VIDEOCC：新增原生节点 phandle，DTB SHA256 0db117f640bef83945ee7df25005393ff5c7e8d85a14abc28d0c5a6f144f60b2，
仅新增该属性，其余原属性逐字节相同；11项测试通过，B/M/R phandle=缺失/b7/缺失。
已部署，使用Android reboot一次；新boot ac554f76-29d6-4699-90e6-3c7079e40847，
Linux6.18.60-sq2-android1、boot_completed=1、bootanim stopped，用户确认进入桌面。
/dev/video0与qcom-venus已存在，VIDEO_READY_NOT_FRAME_PROOF；不得重复PAS加载，实际解码尚未验收。
声卡、两WSA功放Attached及AUDIO_READY；BAT1/ADP1存在，Android100%与内核98%差异仍待对齐。

触摸：用户报告不可用。IPTSD虚拟节点存在且Android启用，但日志有Bad body length3499>124、
8207>8，继而reset/descriptor timeout。直接读代码发现spi_hid_input_body_complete固定解析input，
而header回调把command/feature/report descriptor body读入response。修正为根据真实rx_buf选择
response或input，保留长度检查。此缺陷在所对比旧基线源码也存在，不称6.18新引入。
未修改阈值、校准、iptsd、PM路径、GPU或图形后端。外部构建额外适配私有头路径和SPI trace accessor。

scripts/prepare-sq2-lts-spi-response.py可复现独立模块源；原树未修改。
首次构建缺../hid-ids.h（exit2），已修正并构建exit0。实际完成回调的同输入夹具：
BASELINE valid response选input、-22并reset；MODIFIED选response、0无reset；ROLLBACK恢复原行为。
三次检查exit0；真正超长响应仍-22，正常touch仍通过；这是代码验证不是物理触摸验收。
原件回滚hash一致，源码/整包patch均重建一致。

按用户允许仅热换spi_hid，未卸载SPI控制器、未重置QCA/ADSP、未重启。
新模块048782059cfbc7f01c27f0bd6569d17e6567278544aa390963e30dec170908b0，
实际加载build-id核对一致。check-device首次exit0，iptsd启动、虚拟节点刷新。
重绑定阶段有8次zero-sync错误；daemon启动后观察错误计数8→8，2秒rchar62049142→62753412。
初次25秒getevent未观察到接触事件，BusyBox timeout143使预期124的采集包装器exit1；
这不等于驱动失败，也不证明点击恢复。已请用户试点击/拖动/双指，尚待反馈。
校准efbbfc4e…、iptsd037fecb5…未变。原模块仍在/lib/modules，热载来自/run，ESP未改。

候选initramfs已生成：203848874 bytes，SHA256
39fe0b5a89f85d1cb19a6f31bdd48c86554e101ac2f8d1ea94f1e136d626d234，唯一有效变化spi-hid.ko；
depmod无错误，所有其他条目/凭据/早期网络关闭/音频/SSAM保持。配对DT为0db117f6…。
尚未把该候选写ESP，不宣称冷启动通过。现ESP initramfs仍7b375eb5…。
用户此前对本6.18迁移保留GPU700/432例外；本轮未触及GPU。6.3.8回退和共享固件未变。

证据根 out/kernel-6.18-migration-20261105：touch-runtime-20261106/hot、
touch-response-paired-20261106；最新CHECKPOINT.json承接下一步，不改历史paused-checkpoint。
四稳定角色仍MODIFIED_FILE.dtb、DIFF_FILE.patch、VERIFICATION.txt、ROLLBACK.sh；新增源、模块和
initramfs记录扩展这些角色，回滚新flags为--touch-response-source/module/initramfs。
下一步依据实触反馈决定持久化或继续直接比较，不重复试启旧包、不把服务在线当作触摸成功。

## 2026-11-06：暂停保存点——6.18 新启动成功，视频仍在 HFI 参数检查失败

用户要求“直接对比并修复，然后再尝试启动，不要做实验”，之后明确暂停；本次仅保存进度，
不恢复目标、不访问设备、不继续编译/测试/部署/重启。未提交或推送 Git。
当前分支 codex/kernel-6.18-migration-20261105；下述为最近一次已采集结果，不冒充保存时实时观测。

已部署 /media/esp/EFI/SQ2L618C/initramfs.cpio.gz，203193444 bytes，SHA256
7b375eb508cb2d166157f2c68a226019cfa956e5ee33c4ae6180596467472ca5。
仅在145257f4…音频/电池包上修改 bin/video-decoder-lab-start：复用原生 SC8180X VIDEOCC，
保留旧 DT 的时钟 overlay 分支及固件一次启动保护。原模块确实报 File exists（exit1）；
没有删除 guard 或用重复 PAS 加载试探。部署上传/完整回读/清单通过，Image/DT/EFI/GRUB/
6.3.8 回退未改，GPU700/RPMh432例外保持，早期 Wi-Fi/DHCP/SSH 仍关闭。
远端备份 initramfs.before-video-clock-20261106.cpio.gz 为145257f4…；更早备份均有主机原件。

按用户要求只发送一次 sync && nsenter -t 1 -m -r -- /system/bin/reboot。
SSH命令超时124不是启动结论；随后已实读新boot fc1b798b-21d1-4ebc-ab0a-07318c13fed0，
6.18.60-sq2-android1、boot_completed=1、bootanim=stopped、system_server=3693，回连192.168.5.91。
声卡及两功放 Attached，音频日志 AUDIO_READY；电池/充电器自动注册、resource monitor ready，
Android AC=true、present=true、level100、Full。此次证明重启后自动初始化；最新重启后的听感
未另询问。此前用户“声音恢复了”是前一boot的确认，不能移作本次听感。温度不可用/health未知。

视频尚未修复：原生 VIDEOCC 已绑定，但 sq2_hfi_overlay 加载报 invalid parameter，无 /dev/video*，
未出现 PAS-attempted 标记。直接对比运行 FDT 与已编译 HFI：GCC/SMMU/四互连引用和根中断父
均相符；VIDEOCC 的 compatible/三种#*-cells均符合，但没有 phandle。HFI 的运行时重绑定
要求 live phandle 非零，因此缺失是明确阻断条件；不是凭此宣称全部视频错误已解决。

未完成源码：scripts/prepare-sq2-lts-el2-dtb.py 新增 ensure_video_provider()，给该原生节点按
整树已用编号分配未占用 phandle，并接入 prepare() 返回路径。源码已写入，尚未运行验证、
尚未生成新 DTB、未部署。MODIFIED_FILE.dtb 仍0c2920a1…，实际设备DT也未改这一项。
不要把未验证源码记为已生效修复；也不要沿用旧计划做 live firmware 分段实验。

恢复后第一步：审查/验证上述未完成源码，生成仅新增 VIDEOCC phandle 的独立 DTB，核对所有
已有资源/ABI/属性不变并保留可恢复副本，再按用户“直接比较修复后完整启动”的方式推进。
恢复目标前不得自动执行该步骤。视频真实解码、LTE、Terminal、休眠、跨设备等全基线仍未验收。

保存位置 out/kernel-6.18-migration-20261105/paused-checkpoint-20261106：CHECKPOINT.json、
源码逐字节快照、tracked-source.patch；原始证据继续保留 video-runtime-20261106 和
video-clock-paired-20261106/deployment。四角色仍为 MODIFIED_FILE.dtb、DIFF_FILE.patch、
VERIFICATION.txt、ROLLBACK.sh；视频包 B/M/R 原生时钟分支无/有/无，三次exit0，音频/SSAM/
一次启动保护保留，rollback包哈希恢复145257f4…，二进制patch重建新包一致。

## 2026-11-06：音频/SSAM 电池修正已合包部署；视频仍缺实际设备节点，未再重启

上一检查点的“新增修正仅 RAM”已更新：现已部署到 /media/esp/EFI/SQ2L618C/initramfs.cpio.gz，
202556595 bytes，SHA256 145257f48d03b01ab8c0380742e52d7de657b5e8c2bb3ed0963c4b2d6bd5e01b。
从已启动的 GENFS 包 a6ec5fde…生成，实际有效差异只有 bin/audio-drivers-start、
surface_aggregator.ko、modules.alias、modules.alias.bin；depmod -e -F 匹配 System.map
stdout/stderr 均空、exit0。init、GENFS策略、HAL、其他模块、GPU700/432和所有凭据保持不变。

新增可复现工具 scripts/prepare-sq2-lts-ssam.py + patches/surface-6.18-legacy-of.patch，
在独立副本应用后所有 C/H/Makefile 与已构建并热加载的源码一致，原内核源树不改。
scripts/pack-sq2-lts-audio-ssam.py 负责合包和模块索引，不写设备或重启。5项打包测试、
4项音频提供者发现测试通过。发现原包把密码SSH附件合并进大 gzip member；首次末尾
复用整 member 被有效条目校验拦住（exit1、未写产物），已更正为按原打包器三项文件
逐字节保留 dropbear/start-ssh/shadow 至末尾，防止回盖新策略/模块；没有重新生成身份。

同一有效包检查 B/M/R：音频 OF 发现=false/true/false，SSAM legacy OF alias=false/true/false，
三次 exit0；init/策略哈希相同。ROLLBACK.sh --audio-power-initramfs 在独立副本恢复到
a6ec5fde…，二进制补丁重建新包哈希一致。私有整包已逐 tar 成员回读：
out/kernel-6.18-migration-20261105/audio-power-paired-20261106/SQ2-6.18-audio-power-private.tar，
SHA256 3c8384445dc5ea3fce2aab24a0d8b47aeb9d56ae11a3c2821d467bfb9b58ed34。

部署前重新识别 ESP/root 源及所在物理盘、检查默认菜单和原清单。ESP 空间不足时，仅将
旧48073860…备份完整回读与主机原件比较一致后删除远端冗余副本；新回退备份为
/media/esp/EFI/SQ2L618C/initramfs.before-audio-power-20261106.cpio.gz（a6ec5fde…）。
上传/激活后完整回读、gzip、SHA清单、原内核/DT/EFI/GRUB及各旧入口哈希全部通过。
boot ID 仍 e8b6547a-095e-42b7-8bd6-a2ae39bc3315，未发重启命令；冷启动初始化顺序仍待测。

下一未完项：硬件视频。实读 C2 服务 running，但无 /dev/video*、无 Venus/HFI/identify 模块。
启动日志停在清单检查之后；一次启动目录已创建，PAS-attempted 标记不存在。
有效 helper 接下来 modprobe sq2_iris1_identify allow=1；其编译源码 start() 在发现
/soc@0/clock-controller@ab00000 已存在时返回 -EEXIST，而6.18运行时确有该原生节点
（qcom,sc8180x-videocc + qcom,sm8150-videocc，尚无 driver）。这是明确的路径冲突候选，
尚未采到该 modprobe 的原始错误，不能把推断写成已实测返回值。没有重试固件/PAS。
下一动作是验证并修正“已有原生视频时钟节点”分支，再从尚未启动固件处继续；保留
硬件资源核验和不可重复启动保护，不卸载/重置活动提供者，不用仅 C2 running 冒充解码通过。
完整基线目标仍未完成；视频、LTE、Terminal、休眠及跨设备验收不能由本次音频/电池推定。

## 2026-11-06：6.18 已完成 Android 启动；用户确认声音恢复，电池读取已恢复（新增修正尚在 RAM）

GENFS 修正后的实机为 6.18.60-sq2-android1，boot ID
e8b6547a-095e-42b7-8bd6-a2ae39bc3315，boot_completed=1；本次复核 system_server
仍为 3190，未额外重启。早期 Wi-Fi/DHCP/SSH 保持关闭，Android 自有 Wi-Fi/postboot SSH 可用。

音频：旧包 helper 以 remoteproc2/name=17300000.remoteproc 判断 ADSP，6.18 名称为 adsp，
因此提前退出。改为按 OF compatible、firmware-name 和 attached/running 状态发现提供者，
沿用已接受的 WSA 枚举/初始化流程，不重置共享 ADSP、不改 HAL/增益。两功放 Attached、
实体 ALSA 声卡及 AUDIO_READY 已实测。用户先报破音，最新明确回复“声音恢复了”；
这补充了本次主观播放验证，但未隔离先前破音原因，不等于冷启动/长时播放通过。
60 次 PCM 采样全部在 closed 状态，不能把未见 XRUN 当作连续播放无错误的证明。

电池：6.18 SSAM 遗漏部署 DT 的 surface,aggregator/ssam-uid 兼容路径，导致控制器和
子设备未注册。已在独立源码副本补回 legacy OF UART 参数及子设备枚举，保留新 upstream
绑定逻辑，编译并在确认无绑定客户后仅替换未使用 SSAM 模块；没有操作 QCA/ADSP。
现 ADP1 online=1、BAT1 present=1，Android present=true、AC powered=true、level=100、
status=5（Full），电压 8479mV、charge counter=4232189µAh。温度不可用、health=unknown，
不伪装温度或 GOOD；拔插电源、充放电动态及新冷启动仍待验收。

持久化边界：ESP 仍为 GENFS 修正版 initramfs a6ec5fde…；音频 helper cdf43171… 与
SSAM 模块 a057812e… 当前仅 RAM 生效。音频单项候选 2b91cb9b… 已打包但未部署；
SSAM patch/source/module 已保存并验证二进制补丁重建和独立副本回退，尚未并入 initramfs。
下一步从当前已启动 GENFS 包合并两项修正、重建模块索引、核验所有有效覆盖条目后部署；
不要直接重启丢失现场修正，也不要将本次恢复称为完整基线能力对齐。
GPU700/RPMh432 按本轮用户例外保持，图形后端未切换，未清理应用缓存/凭据。

证据：out/kernel-6.18-migration-20261105/{runtime-618-20261106,audio-provider-20261106,
ssam-legacy-of-20261106}；继续复用 MODIFIED_FILE.dtb、DIFF_FILE.patch、VERIFICATION.txt、
ROLLBACK.sh 四角色。同输入音频发现 fixture 的 BASELINE/MODIFIED/ROLLBACK exit=1/0/1；
SSAM modinfo legacy OF alias 为无/有/无（均 exit=0），两项回退哈希一致。未在活机执行回退。

## 2026-11-06：动画结束卡住定位到genfs标签语义不匹配，修正已部署并请求Android重启

最新6.18 boot07c631ba…记录：bootanim在62.200s exit0，之后system_server退出28次、
zygote被SIGKILL27次，所采218s内未见kernel panic/GPU watchdog；不是“仍停在bootanimation”。
读取回退系统DropBox保存的两个previous system_server native tombstone，内部Kernel Release
为6.18.60，栈位于ClatCoordinator+720(SIGABRT)。外部DropBox header为采集时6.3.8，
不能误判为回退崩溃。实际APK/APEX库反汇编定位为BPF路径/属性/SELinux标签检查后abort。
该次audit的CLAT map标签确实为unlabeled；当前6.3.8同对象为fs_bpf_net_shared。

根因链：android-host-policy.c为6.3移除了genfs路径末尾*，但保留policycap
GENFS_SECLABEL_WILDCARD。6.18支持该位，/net_shared被当作精确匹配，子对象不再命中。
本次只清除已转换策略的该能力位（1byte，1bit），恢复6.3已有prefix语义；converter同步修正。
不绕过Android检查，不修改策略规则/permissive map/凭据，不热重载当前系统策略。
新增scripts/fix-sq2-lts-genfs-policy.py及4项单元测试。真实包policy解析+相同五CLAT路径
检查B/M/R：5/0/5标签错误，exit1/0/1；可执行ROLLBACK --genfs-initramfs恢复hash一致。
libsepol3.11严格解析发现原有all-types实验permissive含属性位；只在检查器内存副本移除
这些属性位后完整validate通过，部署二进制没有改这些位；不得称为生产enforcing策略。

已替换/media/esp/EFI/SQ2L618C/initramfs.cpio.gz，201841340bytes，SHA256
 a6ec5fdeec8c0f089c5dd43011863b531b1d393f4c56ec9603a79b98e4235479。
原48073860…保留initramfs.before-genfs-20261106.cpio.gz。完整上传回读、gzip、SHA清单通过，
Image/DT/EFI/GRUB及旧入口hash不变；早期Wi-Fi/DHCP/SSH仍关闭，日志保留，正常Android
Wi-Fi/postboot SSH保留。GPU700/432仍按用户例外不改；没有切换Mesa/高通后端。
ESP空间不足：仅将旧60af备份整文件回读并验证与主机original/initramfs-before-disable-early.cpio.gz
完全相等后删除ESP冗余副本，保留本轮现场48073860备份；不删除fallback或用户数据。

按最新用户要求，已发送一次sync && nsenter -t 1 -m -r -- /system/bin/reboot。
具体返回及新boot观测见out/kernel-6.18-migration-20261105/animation-stall-20261106/deployment。
重启前boot592629d8…(6.3.8)，后续必须用新boot ID+uname+boot_completed确认，不能把
主机标签fixture或SSH断线当作真机启动成功。完整基线能力仍待运行验收。
分析/二进制差异/两次tombstone/匹配EFI文件集均在animation-stall-20261106；原四角色继续扩展。

### 本次试启补充：重启请求尚未生效

svc power reboot exit0后两次读回仍是6.3.8和原boot ID bb27cb2c-670c-4c83-ae47-152173054263；
Android boot_completed=1、sys.powerctl为空，所查system log尾部无shutdown/reboot匹配（grep exit1）。
因此不能称已重启或正在6.18启动。已请在场操作者手动重启选第一项，未追加强制重启。
新默认已安装，第二项6.3.8回退仍完好。下一步以实际新boot ID/现场画面为准，不重复发送reboot。
## 2026-11-06：完整6.18候选已部署，用户在场授权一次试启

用户明确回答“现在可以试启”。SSH严格复用原主机密钥核对10.126.126.1为SQ2，
当时运行6.3.8回退，boot bb27cb2c-670c-4c83-ae47-152173054263；本次ESP为
/dev/nvme0n1p6、系统loop来源/dev/nvme0n1p3，挂载/分区/空间已读回。
候选安装到独立/media/esp/EFI/SQ2L618，所有载荷逐文件完整回读一致、SHA清单和gzip校验通过。
旧SQ2MesaDev和SQ2Fallback全部文件哈希不变；只把GRUB默认chainloader路径换为
/EFI/SQ2L618/start.efi，两项菜单/6.3.8 fallback保持。
菜单原件备份/media/esp/EFI/BOOT/grub.cfg.before-618-paired-20261106，主机同存trial-20261106。
仍Mesa/60Hz，无图形后端切换，未触发全应用图形缓存清理。

已执行一次sync及Android svc power reboot，命令exit0；这不是新内核启动成功证明。
等待现场画面及新boot ID/内核/Android结果，不自动重复重启。失败请操作者记录末屏后
手动第二项回退；恢复SSH后采集日志并修正，禁止从SSH暂不可达推断死机根因。
DEPLOYMENT.json、reboot-request.json及完整前后读回在trial-20261106。
四角色保留，新增GRUB.patch/GRUB-BMR.json；ROLLBACK.sh --grub-config TARGET_COPY
已在独立副本验证：B/M/R均两项且fallback存在，candidate false/true/false，恢复原hash。
目标未完成，不宣称真机6.18已启动或功能对齐。
## 2026-11-06：启动策略审计完成，新增路径一致的完整SQ2 EFI候选

实读io-paired包纠正此前“旧LTE/保活待移植”的过宽判断：init没有lab-keep-awake或
framework-suspend-start启动钩子，Codec2嵌套runtime已是v2u且无must-Y门禁；LTE seed
已读取本机DMI UUID缓存并经sq2-fsg-cache验证，guard按机型/EL2/nohibernate检查，
不按单机身份准入。文件名lte-unit-*本身不能证明旧单机绑定。未重写这些已正确路径。
现存终端用户选择、USB分支及公共镜像身份分离仍需单独核对，尚不称通用交付。

实际发现：旧payload里的router/launcher指向SQ2MesaDev，而旁边kernel-options.txt
指向SQ2Lts618；只复制该目录会混用路径。新增scripts/build-sq2-lts-efi-bundle.py，
把两份未签名ARM64 PE中的SQ2路径同一重定位至EFI/SQ2L618，文本配置从真正的
EFI命令串导出；原SQ1分支不改，故此包仅交付SQ2，不声称包含SQ1候选。
只改完整UTF16字符串并检查长度、PE布局、无签名/校验和及所有区间外字节不变。
已签名tcblaunch.exe和qebspilaa64.efi逐字节保留。签名负例拒绝，重复生成一致。
有效命令保持screen_only=0、screen_only_autosuspend=0、nohibernate、正常EL2、panic0。

最新启动包：out/kernel-6.18-migration-20261105/efi-20261106/
SQ2-6.18-complete-EFI-private.tar。EFI/SQ2L618含完整router、launcher、TCB、固件loader、
GENI/eDP内核/模块配套initramfs、700/432温控DT、实际命令和SHA256SUMS。
Image a3eb8bd6…、initramfs afbce1c3…、DT 0c2920a1…与io-paired一致。
B/M/R同路径一致性检查exit1/0/1；回滚副本恢复三份原文件hash一致。
四角色沿用，EFI.patch/BMR.json扩展记录；ROLLBACK新增--efi-pair TARGET_DIRECTORY。
未连接设备、未部署、未重启；这证明构建路径一致，不证明Linux/Android或外设启动。
下一步先核对现机/ESP与保护回退，再做隔离候选启动验收；继续休眠及完整基线能力审计，
保持700/432例外与私有资料边界。目标未完成。
## 2026-11-06：GENI整树与eDP速率表编译完成，已重新配对initramfs

上一记录的session43419已结束，make Image modules dtbs exit0（有已有KSU/ashmem等告警，
不称零告警）。eDP新增prepare-sq2-lts-edp.py，从实际6.18源码复制独立msm外置构建，
接入接收端DP_SUPPORTED_LINK_RATES、保留原表索引、PHY支持交集、端点和LTTPR上限，
短写返回-EIO，表外速率拒绝，降档沿接收端表；非eDP/非Surface仍原路径。
没有套用被否定的V6协议选择实验，没有改面板时序或自动改65Hz。
独立msm模块编译exit0；最终针对6.18-io重新编全部十组外挂模块均exit0，未放宽modpost。

提取实际msm_dp_aux_link_configure函数C桩验证6个案例：原版只写BW_SET，修改后
表内540000使用原索引6写LINK_RATE_SET；两种短写返回-EIO、底层错误保持、表外速率-EINVAL。
BASELINE→MODIFIED→ROLLBACK保留原非表路径、修正表路径、恢复原行为，回滚源码hash一致。
速率解析器测试包括PHY交集、最大限制、降档、重复/空/非法表，均通过；不是屏幕实测。
原四角色继续保留，总DIFF增加3个DP源码及helper，重建逐字节校验；ROLLBACK新增--edp-ctrl。

最新成套目录 out/kernel-6.18-migration-20261105/io-paired-20261106/，
SQ2-6.18-io700-private.tar包含GENI内核、eDP模块配套initramfs、700/432温控DT和清单。
Image SHA256 a3eb8bd65adfbb10819d1de1cfebdb6634e76685850c52126bb4f25730ab178e；
initramfs SHA256 afbce1c3bc3da8380b6b403568178494d175839a232d2b43b216cc5531ac41c2。
DT保持0c2920a1…，511模块文件/493唯一模块同6.18 vermagic、旧ABI0，depmod和脚本语法校验通过。
491个未改非模块条目/权限保持，4个cpio覆盖层顺序保持；凭据/私有身份不改，仍仅私有候选。
QCA已经是上游最终失败清理路径，不另改。未部署、未重启、未宣称显示或休眠已修复。
下一步：完整休眠/启动策略与其余6.3.8能力差异审计，清理旧runtime单机假设并接上标准通用
本机LTE采集路径，之后做真实SQ1/SQ2启动与外设验收。不要把现在私有runtime当通用产物。
## 2026-11-06：QCA已含上游清理，GENI晚到DMA完成修正已验证、正在编译

当前对象仍为6.18迁移，GPU保持700/432。对实际源码检查发现QCA qca_setup的out分支
已在每次失败时先qca_power_shutdown，再判断是否重试，最终返回原ret；因此无需重复
添加旧6.3.8清理补丁。源码与SHA及具体分支记录io-20261106/QCA-audit.json。

GENI qcom_geni_serial_handle_tx_dma仍会在tx_remaining=0时调用DMA unprep。
patch-geni-tx-dma-completion.py新增6.18分支，在独立源码副本围住所有权消费块；
保留现有tx_dma_stale/kfifo防下溢、后续start_tx/wakeup、零DMA地址的有效语义。
原src/linux-6.18-parity不改，新副本src/linux-6.18-io；输出build/6.18-io。
提取真实函数、使用主机C桩，同输入四案例B/M/R：零长度晚到DONE的unmap/advance
1/1→0/0→1/1；正常8字节、flush后fifo0、stale三案例行为不变。三组gcc -Werror及执行均exit0。
原四角色继续保留：总DIFF增加GENI补丁，实际git apply重建源码逐字节一致；ROLLBACK.sh
新增--geni-source TARGET_COPY，执行后恢复原hash。证据在io-20261106/BMR.json。

make Image modules dtbs已经启动，工具session=43419；最后检查仍运行（make PID154131，
python PID154113）。不要因安静日志重启构建。先轮询该session，若句柄失效再检查
io-20261106/build-result.json和实际进程；构建完成后重编/校验配套外挂模块并重组包。
本节不是编译完成声明；当前可用成套包仍gpu700-20261106，尚不含新GENI修正。
eDP链路速率表确实仍缺，需保留端点/LTTPR限制并重新实现；不要套用明确被否定的V6协议实验。
其余休眠/基线能力审计、SQ1/SQ2实机验收未完成。未部署/重启，目标active。
## 2026-11-06：用户指定6.18采用700基线，不回退661/416

最新目标：开发补全6.18对齐基线功能，除了GPU频率基线。用户明确“跳过GPU661/416 使用基线700”。
本迁移候选采用历史700MHz/RPMh432（661档也为432，590档416），不再以661/416为目标。
这一选择不证明700稳定；2026-11-05的GMU超时记录保留。未改标准6.3.8全局默认或设备入口。
prepare-sq2-lts-thermal-dtb.py默认baseline-700，validated-661仅显式选项；三项测试通过，
验证不同phandle、错误映射拒绝、700/432和显式661策略。不能把基线称为OEM电压曲线。

GPU/CPU温控DT已补：GPU-top 75C/5C迟滞，两CPU cluster 80C/5C，TSENS轮询1000ms，
CPU/GPU冷却对象引用由当前DT取得。保留6.18原生GPU-bottom和hot/critical保护、SMMU与EFI握手。
并修正initramfs Thermal HAL的gpu-thermal-top为实际6.18 gpu-top-thermal，避免GPU温度缺失。
暂停前的thermal-20261106构建/校验已完成，恢复时确认无遗留构建进程；该661包不再是当前候选。

最新成套产物在 out/kernel-6.18-migration-20261105/gpu700-20261106/，
SQ2-6.18-700-private.tar包含Image、匹配initramfs、DTB、config及manifest。
DTB SHA256 0c2920a12ace69a638bb1d69dae1e86fbb738b9836b7de17e599e9524e058996。
Image/initramfs与已完成温控包逐字节相同，仅改变DT GPU档位与清单；本轮未部署/重启。
原四角色继续使用，前版归档gpu700-20261106/previous-*；DIFF重建与独立副本ROLLBACK执行通过，
原始BASELINE GPU514 → MODIFIED GPU700并带温控 → ROLLBACK GPU514且原hash一致。
这些是DT静态/包内验证，不是真机频率、热调节或稳定性验收。目标仍active，后续继续
显示eDP/QCA/GENI/休眠差异、其他6.3.8能力与SQ1/SQ2验收，不再推进661作为目标。
## 2026-11-06：配套 initramfs 与视频/模块适配已生成，未实机验收

新增成套候选在 out/kernel-6.18-migration-20261105/paired-20261106/。
Image SHA256 8842e6045209ade2786f46ec69550b779ee557eff8b8139ec34a594d03855a7c；
initramfs SHA256 130f458d511705cae052d9375e02cba5c44a4757bb989e6cc50849811a8fd180，201329741 bytes；
DTB沿用CPU候选 e1fdd29c…，并非GPU661/温控完整对齐DT。
成套Image/initramfs/DTB/config及MANIFEST打包为SQ2-6.18-paired-private.tar。
这是保留原身份/凭据的本地私有候选，不是通用公共镜像；尚未部署、重启或验收功能。

本轮完成：
- 从原HFI模块提取原overlay为参考，逐属性比较，只允许provider引用cell变动；IRQ、SID、
  资源ID和其余属性一致。没有伪造缺失DSDT。按实际6.18 DT重生成HFI/固件overlay。
- VIDEOCC新源码的两处HW_CTRL_TRIGGER按原基线软件管理方式改0，保持时钟和状态检查。
  HFI、firmware overlay、VIDEOCC、RMNET以最终6.18配置编译成功，未放松modpost。
- 配套完整模块树，替换原18个旧ABI模块。最终511个模块文件/493个唯一模块全部同一
  6.18.60-sq2-android1 vermagic；depmod -e -F System.map无stderr、exit0。
- init的TSENS旧路径改为modprobe qcom_tsens；视频helper绑定6.18 release并改用modprobe，
  以自动加载videodev新增mc等依赖；模块树使用同一份定制Venus/VIDEOCC，避免加载另一版本。
- 更新视频SHA256SUMS及LTE runtime.sha256；校验所有SHA256SUMS与被修改的.sha256清单。
  不改变未改动runtime.tar内文件的嵌套清单。492个未改非模块条目内容、权限等保持一致，
  输入/输出均4个cpio trailer，原覆盖顺序和私有身份保持。gzip完整解压重读一致。

同一审计命令BASELINE/MODIFIED/ROLLBACK的旧ABI模块数18/0/18，旧release脚本引用
2/0/2；回滚副本hash回原件。原四角色继续保留，ROLLBACK.sh新增
--initramfs TARGET_COPY，从original/initramfs.cpio.gz校验hash后恢复。新验证为静态包审计，
不是三次实机启动。辅助证据BMR.json、helpers.patch、commands.log和overlay参考审计在成套目录。
仍未完成GPU661/416及温控DT、eDP/QCA/GENI/休眠全基线审计与SQ1/SQ2实机验证。
不得称为全部6.3.8能力已对齐或启动问题已实测修复。前次设备暂存包不会自动更新/生效。
## 2026-11-06：SQ2 组网地址已连通，仅暂存未激活

用户要求向10.126.126.1部署变更。复用原设备SSH主机密钥严格验证连接，运行中为
6.3.8-sq2-android1、SQ2Fallback，boot ID bb27cb2c-670c-4c83-ae47-152173054263。
动态读取挂载与分区信息，ESP实际为/dev/nvme0n1p6，/media/esp。
现有编译包没有匹配initramfs，故未替换Image、DTB或启动配置，不宣称部署生效。
仅上传到 /media/esp/EFI/SQ2Lts618Staging-20261106/PARITY-COMPILE-ONLY.tar.gz，
附MANIFEST和NOT-BOOTABLE说明；25,864,370字节，SHA256
6e09fac5322912e2fe17302e0d858f4867a40f0862bd603d1f2fe706b6e66bfd。
整包SSH回读逐字节一致，默认/回退/EFI BOOT所有文件hash与boot ID前后完全一致。
没有重启、切换图形后端或清理应用缓存。证据在原四角色VERIFICATION.txt及
out/kernel-6.18-migration-20261105/deploy-20261106/，RESULT为STAGED_ONLY_NOT_ACTIVATED。
后续仍须完成前节配套链和initramfs后才能激活；不要把暂存目录当启动入口。
## 2026-11-05 最新：按 6.3.8 基线补齐编译，完整对齐仍未完成

用户所说“3.8基线”本轮按项目现有 Linux 6.3.8 基线处理。不是 Linux 3.8。
对照实际 6.3.8 .config、启动脚本和最后生效的 cpio 条目，确认原 6.18 包除
KVM/vhost-vsock 外还缺少媒体、旧 iptables 和 KernelSU 配置；存在 18 个旧 ABI
外挂模块、硬编码的旧 release 路径以及视频 helper 的旧版本判断。只换 Image 不可交付。

最新独立源码 work/lts-6.18-20261105/src/linux-6.18-parity、构建目录
work/lts-6.18-20261105/build/6.18-parity-ksu 的 Image/modules/dtbs 编译 exit0。
恢复 KVM/VHOST_VSOCK、内建 KernelSU、IPv4/IPv6 iptables、media/Venus/VIDEOCC。
IPA4.1、定制 Venus、TSENS 和电池适配模块针对此最终配置重新编译均 exit0。
这仅证明编译；不证明设备节点、LTE、硬解、温控或 KernelSU 真机功能。
CPU DT 增补基线的 3072/3148.8 MHz OPP，保留硬件 LUT 电压来源；未伪造电压。

编译检查点：out/kernel-6.18-migration-20261105/PARITY-COMPILE-ONLY.tar.gz，
25864370 bytes，SHA256 6e09fac5322912e2fe17302e0d858f4867a40f0862bd603d1f2fe706b6e66bfd。
归档已重开并逐项核对内容。MANIFEST 明确标记 COMPILE_ONLY_INCOMPLETE_PARITY_NOT_DEPLOYABLE；
没有 initramfs，不能作为启动整包或仅替换设备 Image。

仍待完成：HFI/固件/VIDEOCC overlay 配套链及动态资源引用审计；GPU661/416和CPU/GPU
温控DT；eDP link-rate适配；QCA/GENI及剩余休眠补丁审计；所有外挂模块ABI、启动helper、
完整性清单和最后覆盖顺序一致的initramfs组包。HFI生成器缺原SQ1 DSDT参考输入，
当前workspace/out搜索未找到；没有删除参考校验或冒充完成。SQ1/SQ2实机验收未做。

继续复用原四角色，parity-followup保存前版及新增日志。当前MODIFIED_FILE.dtb为CPU候选，
SHA256 e1fdd29ca14b6562516e547964926c0f8b56bfcdb44a6bd5c15ece36cf8efb0e，88949 bytes。
DIFF_FILE.patch可重建原DTB→当前候选及原6.18配置→最终配置，实际git apply核对通过；
ROLLBACK.sh在独立副本恢复DTB和配置均exit0且hash回到原件。
相同配置检查B/M/R：7项功能原版unset，修改后5项y、Venus/VIDEOCC为m，回滚再次unset。
这是静态配置行为，不是三次实机启动。先前EFI夹具14/0/14、SMMU谓词19/0/19、
终端脚本模拟1/0/1证据保留，不能替代硬件测试。

构建中旧KSU Kbuild曾自动修改原6.18源码的两个头文件，已从固定6.18.60源码归档逐字节
恢复并保存证据；新副本关闭该自动sed逻辑，按新SELinux结构移植并采用内建编译。
不得重新启用自动修改原树的旧Kbuild；不通过放松modpost来绕过ABI。
本轮未部署、未重启、未切换图形后端；设备默认DTB仍是前次446b3649…版本。
后续从上述未完成项继续，不能把此检查点称为“已理论功能对齐”或“已修复无法启动”。
## 2026-11-05 最新：SMMU已越过，终端准备因虚拟化配置缺失受阻

用户日志确认CB allocation limited to106/physical127，随后完成系统盘选择，最终
`SQ2_NATIVE_PREP_FAILED: el2-terminal-stage`。没有证明显示/触摸/Android已通过。
实际initramfs最后生效脚本依次检查Venus文件、/dev/kvm、/dev/vhost-vsock；前两文件存在，
但实际6.18配置关闭VIRTUALIZATION和VHOST_MENU，KVM/VHOST_VSOCK未构建。
这是确定的能力遗漏；尚未读到现场/logs/el2-terminal.txt，不宣称排除了其他失败。

已在原源码的独立O目录 work/lts-6.18-20261105/build/6.18-avf 生成候选：启用
VIRTUALIZATION/KVM/VHOST_MENU/VSOCKETS/VHOST_VSOCK=y。make Image modules exit0，
0 error/8 warning（完整构建暴露已有ashmem/SPI相关告警，非零告警构建）。
Image 35289600 bytes，SHA256=4f29b73afcc356bea8567422ba222c36332658a8149696c93d0b4ef20e8fcad0；
vmlinux实际包含kvm_dev_ioctl和vhost_vsock_dev_open。原Image ec54afb3…完整保留。
包内原helper夹具B/M/R exit1/0/1；设备节点按配置模拟，非实际KVM运行证明。
四角色继续保留，terminal-followup/MODIFIED.config、BASELINE.config、CONFIG.patch、
build-result.json为补充；总DIFF可重建原DTB与.config，ROLLBACK.sh新增
`--terminal-config TARGET_COPY`恢复原配置，既有DTB单参数回滚不变。

SSH再次超时exit255，未写设备、未重启。新Image还未与新modules重打包；不能单换Image。
下一步：准备匹配的模块/initramfs并审计原有外置6.3.8模块；用户从回退恢复SSH后部署配对候选。
不要删terminal-stage检查或禁用KVM来冒充正常终端能力恢复。CPU电压/SPI头错误另列待查。

## 2026-11-05 最新部署检查点：SMMU DT 开关候选已部署，未重启

用户确认SSH恢复；实读设备仍为6.3.8/SQ2Fallback。核对ESP挂载、当前DTB及载荷hash后，
仅原子替换默认 `/media/esp/EFI/SQ2MesaDev/android-el2.dtb`，SHA256
`446b364978bc61469024497294c91c7c5aaa292b4c3f7511d8e557096bab74ce`，88821 bytes。
完整回读与候选一致；原件留同目录 `android-el2.dtb.before618-smmu-20261105-6c6afe67`。
默认Image/initramfs/EFI、GRUB和全部回退文件hash不变，boot ID不变，未重启、未切图形后端。
复用既有四角色，新增 smmu-followup/device-before.txt、device-after.txt、device-readback.dtb。
主机B/M/R仍为拒绝/允许106并保留21/拒绝（exit19/0/19）；不代表硬件SMMU或黑屏已修复。
下一步手动选择默认入口复测，观察106/127错误是否消失、是否出现context pool提示及后续日志。

## 2026-11-05 最新：进入 Linux 后 SMMU probe 失败并黑屏（新候选未部署）

用户报告上一握手修正后已出现 Linux 日志，随后黑屏。主 SMMU 15000000 报
`found only 106 context irq(s) but 127 required`；该分支返回 -ENODEV。
已移植内核包含 IRQ-backed context pool 保护，但 DT 遗漏其 opt-in。
准备器新增显式 `--irq-backed-context-pool`，只添加
`/soc@0/iommu@15000000:microsoft,el2-irq-backed-context-pool`，不修改 IRQ/reg，
不关闭 IOMMU；内核仍校验 EL2、SC8180X、127/106/0 硬件组合，保留物理127 banks，
仅允许分配有 IRQ 的106个，保留21个不可分配。黑屏因果尚未完整验证。

沿用 out/kernel-6.18-migration-20261105 四角色，旧版本归档 smmu-followup/。
新 MODIFIED_FILE.dtb SHA256=446b364978bc61469024497294c91c7c5aaa292b4c3f7511d8e557096bab74ce。
真实内核纯谓词主机检查 B/M/R：拒绝/允许106并保留21/拒绝，exit19/0/19；
原 RMTFS 检查仍通过，7项回归通过，patch重建与rollback恢复原hash一致。
这是主机检查而非完整 SMMU probe 或显示恢复。SSH连接超时exit255，新候选未部署、未重启。
下一步用户从原回退入口恢复连接，再仅部署该DTB并记录是否越过SMMU错误；
若仍黑屏则采集后续显示/根挂载日志，不盲目重复启动或禁用IOMMU。

# 内核整树迁移到 Linux 6.18 LTS（SQ2）：编译里程碑记录（2026-11-05）

状态：**核心编译通过（Image + 323 模块 + DTB）；首次默认启动在 EFI preboot 失败，握手 DTB 已修正部署，尚待复测。整树功能迁移未完成。**

本文只记录"源码/构建"层面已经完成和尚未完成的事，不构成任何功能已通过的结论。
用户指令为"整树迁移到 msm 6.18 的最新 LTS，先全部完成过编译，再真机启动 debug"，
最初编译阶段没有部署；随后已部署并发生首次启动失败。以下 §0 为最新确认状态，
§1–7 的阶段记录不能代替本节，也不能当作 Linux 已成功启动。

## 0. 当前检查点：EFI preboot Not Found（2026-11-05）

用户报告默认入口停在 `EL2 BOOT ERROR [preboot prepare]: Not Found (0x800000000000000E)`，
随后手动从回退入口恢复。本轮 SSH 只读确认运行 `6.3.8-sq2-android1`、cmdline 指向
`SQ2Fallback`；磁盘默认 `SQ2MesaDev/Image` 已是 `ec54afb3…f838b49` 的 6.18。
GRUB 也已改为两项、6.18 默认，故 §7 的“三项、默认未改”只是此前中间态，不再代表设备。
该错误发生在 qebspil 的 `StartImage`，尚未执行 Linux `LoadImage`，不能归因于 msm 或模块 ABI。

### 已完成的定点更正

- 实际默认 DTB `e91bd896…87387d53e` 仅带 6.18 板级资源，遗漏了 EFI 握手属性。
  `probe/virtualization/sq2-preboot-rmtfs.c:sq2_rmtfs_prepare()` 检查
  `/reserved-memory/memory@85500000` 的 `microsoft,el2-preboot-rmtfs`；缺失即返回
  `EFI_NOT_FOUND`。它在本次原 DTB 中确实缺失，三 DSP 的 opt-in/result 属性也全部缺失。
- 新增 `scripts/prepare-sq2-lts-el2-dtb.py INPUT_DTB FRESH_OUTPUT_DTB`，给匹配的
  Surface Pro X / SC8180X 新 DTB 添加 18 项配对 EFI ABI 属性：RMTFS/DSP opt-in、
  `qcom,broken-reset`、四节点的零值 complete/status、根节点 kernel-fdt 空槽、
  PAS trace 未执行哨兵、Venus metadata contract。**不预填认证成功，不复制旧 phandle/资源。**
  拒绝非匹配硬件、错误 RMTFS 边界/ownership、非 pristine 状态及覆盖已有输出。
- 属性级逐项比较：18 新增，0 个原属性改动或删除，0 个节点变化；6.18 的 reg、phandle、
  memory-region、firmware-name、OPP 全部保留。源 DTB 原字节不变。
- 使用真实 `sq2_rmtfs_prepare()` C 源码和真实 libfdt 做主机测试；仅 EFI 分配/缓存 API
  是主机桩，不执行 SCM。相同程序输入各 DTB：BASELINE `0x800000000000000e / exit14`，
  MODIFIED `0x0000000000000000 / exit0`，ROLLBACK `0x800000000000000e / exit14`。
  可执行 rollback 在独立副本恢复原 SHA；二进制 patch 重建和重新应用候选均字节一致。
  7 项单元测试通过。这不是 EFI 实机执行、DSP 认证或 Linux 启动通过。
- 动态核对远端挂载的 ESP/block sysfs、当前回退 boot 与原 SHA 后，只原子替换
  `/media/esp/EFI/SQ2MesaDev/android-el2.dtb`，新文件 88771 bytes，SHA256
  `6c6afe671f01324f895dc8da859b73fc54d6b5fde193abc00dbfa8f253b78e4f`。
  原件保留为同目录 `android-el2.dtb.before618-handshake-20261105-e91bd896`。
  完整远端回读与候选逐字节一致；默认 Image/initramfs/EFI、GRUB 和全部回退文件 SHA 未变。
  本轮 boot ID 未变，**没有重启、没有切换运行中图形后端、没有清理用户数据。**

### 验证角色与下一动作

固定四角色（后续扩展而不替换）：本仓库绝对路径
`C:\Users\Administrator\Documents\ChatGPT\chromebookOS\out\kernel-6.18-migration-20261105\`
下的 `MODIFIED_FILE.dtb`、`DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`。
补充文件保存原 DTB、原/新 DTS、主机 C 程序与桩、完整部署前后输出及远端回读 DTB。

下一步是修正后首次启动复测（**尚未执行**）。假设：缺失的配对 EFI DT 握手是当前
`preboot prepare/Not Found` 的直接触发条件；变量仅为上述 DTB 属性。区分性观察：
是否越过 `preboot prepare` 到达 `Kernel LoadImage succeeded`，之后分别记录 PAS/TCB/Linux
的下一条原始状态。若再报错则保留完整阶段/状态码并回到原回退，不连续盲重启、不热重置 DSP。
在 TSENS/热区未接回前不做 GPU/CPU 压力验收；§5 其余迁移缺项全部仍未完成。

## 1. 基线与产物

| 项 | 值 |
| --- | --- |
| 内核基线 | `linux-6.18.60`（kernel.org v6.x 最新 6.18.y，2026-11-05 拉取） |
| 源码树 | `work/lts-6.18-20261105/src/linux-6.18-sq2`（ext4 工作区，未入 Git） |
| 对比基线 | `work/lts-6.18-20261105/base/linux-6.3.8`（纯净 6.3.8，用于逐文件枚举项目差异） |
| 构建输出 | `work/lts-6.18-20261105/build/6.18`（`O=` 分离构建） |
| 产物目录 | `work/lts-6.18-20261105/out/linux-6.18.60-sq2/`（含 SHA256SUMS 与打补丁后的源码 tar.xz） |
| 运行版本串 | `6.18.60-sq2-android1`（沿用 `CONFIG_LOCALVERSION="-sq2-android1"`） |
| Image | 34112000 bytes，sha256 `ec54afb324c00bc7a6e92117a33b05f8bc4256d97a016b8ab81abfb46f838b49` |
| board DTB | `sc8180x-surface-pro-x.dtb` 87709 bytes，sha256 `05043631e59306c046861f18972ec93e3d9f1b4f5c5737d24a22cd2de96f7264` |
| 模块 | 323 个 `.ko`；`sq2_battery_compat.ko` sha256 `b34553b6f6a7b5fef5345fbdf852de4fc74608f16f3f6a8ba4dd63ca47884f5d` |
| 编译告警 | `error: 0`，`warning: 0`（`make Image modules`，GCC 13.3 aarch64） |

工具链：Ubuntu 24.04 WSL，`aarch64-linux-gnu-gcc 13.3.0`，64 核并行。

## 2. 移植方式（可复现）

```
# 1) 6.18.60 + 项目 DTS
python3 out/_wsl/prep618.py         # 建立 6.18 工作树并对每个差异文件生成 6.3.8 侧补丁
python3 out/_wsl/setup_dts2.sh      # 把项目在 sc8180x.dtsi 上的音频增量补进 6.18 的 dtsi
# 2) 配置
configs/sq2-internal-kernel.config -> olddefconfig（6.18 Kconfig）
scripts/config -m SPI_HID -m RTC_DRV_SURFACE -m QCOM_LMH
# 3) 项目补丁（分两批，全部带 marker，可重复执行）
python3 out/_wsl/port618.py         # GPU/显示/音频/时钟/蓝牙/ashmem/spi-hid
python3 out/_wsl/port618b.py        # EL2 preboot 交接 + SMMU context pool
# 4) 构建
make -C TREE O=BUILD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j64 Image modules
```

> `out/_wsl/` 在 `out/`（已忽略、不入 Git）下，是本次一次性使用的脚本；真正的改动已经
> 落在 6.18 工作树里，并通过 `out/linux-6.18.60-sq2/linux-6.18-sq2-patched-src.tar.xz` 归档。

## 3. 逐项移植结果

### 3.1 设备树

- 以 **6.18 自带的 `sc8180x.dtsi`** 为基线（6.3.8 时代那份是更早的上游回移，已过时），
  只把项目自己的 4 个 hunk 补上去：`slimbam`/`slim-ngd`、`apr`+`q6afe`+`q6asm`+`q6adm`、
  `sound` 节点与两个 dt-bindings include。
- `sc8180x-surface-pro-x.dts`（1455 行）与 `sc8180x-wcd9340.dtsi` 原样带入，只改 4 处 **USB PHY 标签
  改名**（6.18 上游改了 multiport PHY 的 label，地址也修正为 0x88e8000/0x88eb000）：
  `usb_mp0_hsphy→usb_mp_hsphy0`、`usb_mp1_hsphy→usb_mp_hsphy1`、
  `usb_mp0_qmpphy→usb_mp_qmpphy0`、`usb_mp1_qmpphy→usb_mp_qmpphy1`。
- `make dtbs` 全量通过，本板 DTB 无 DTC 告警。
- 6.18 的 `sc8180x.dtsi` 已自带 `mdss_edp`（`qcom,sc8180x-edp`）、`edp_phy`、`aux-bus` 结构，
  与板级 DTS 的用法兼容，eDP 面板节点未做结构性改动。

### 3.2 内核配置

- 6896 行项目配置经 `olddefconfig` 落到 7698 行。291 个符号被 6.18 移除（绝大多数是自动探测/
  内部符号，如 `AS_HAS_*`、`ARCH_HAS_*`、`CRC32_*` 备选），11 个符号取值变化
  （`ARCH_FORCE_MAX_ORDER 11→10`、`BASE_SMALL 0→n`、`CEC_CORE m→y` 等），均无构建影响。
- 新增/恢复：`CONFIG_SPI_HID=m`、`CONFIG_RTC_DRV_SURFACE=m`、`CONFIG_QCOM_LMH=m`。
- `CONFIG_QCOM_TSENS` 在 6.18 仍依赖 `NVMEM_QCOM_QFPROM`，而项目配置有意关闭 QFPROM，
  所以照旧走树外模块路线（见 §5，**尚未重建**）。

### 3.3 已移植的项目补丁（均在源码里留了 marker）

| 位置 | 内容 | marker |
| --- | --- | --- |
| `kernel/power/suspend.c` | screen-only 系统挂起隔断（只读模块参数） | `SQ2_SCREEN_ONLY_V1` |
| `adreno/a6xx_catalog.c` | A680 使用 `a680_sqe.fw`/`a680_gmu.bin` | `SQ2_A680_FW` |
| `adreno/a6xx_gpu.c` | ucode 版本检查接受 `a680_sqe.fw` | — |
| `dpu1/dpu_plane.c` | 去掉 `DRM_FORMAT_MOD_QCOM_COMPRESSED` 修饰符 | `SQ2_NO_UBWC_MODIFIER` |
| `drm/panel/panel-edp.c` | EDID 读失败重试 10×100ms；新增 `LP129WT112684` 面板条目 | `SQ2_EDID_RETRY` |
| `sound/soc/qcom/sdm845.c` | 停播前先静音 WSA PA（`--machine-only` 语义），两处 `sdw_disable_stream` 前 | `sq2_mute_sdw_speakers` |
| `clk/qcom/gcc-sc8180x.c` + dt-bindings | 补 `GCC_USB3_MP0/MP1_CLKREF_CLK`（0x8c000/0x8c004） | `gcc_usb3_mp0_clkref_clk` |
| `bluetooth/hci_qca.c` | WCN3998 重开时的异步 baudrate ack 丢弃（只在 HCI_INIT 期间） | `SQ2: consumed WCN3998` |
| `mm/{shmem_fs.h,memfd.c,shmem.c,Makefile}` + `mm/sq2-memfd-ashmem.c` | Android ashmem 兼容 ioctl（仅普通 memfd inode 生效） | `SQ2_ASHMEM_SHIM` |
| `drivers/hid/spi-hid/` | 项目自带的 spi-hid 驱动（含 `SQ2_SPI_PM_V1` 系统睡眠恢复补丁）；`strlcpy→strscpy` | `SQ2_SPI_PM_V1` |
| `drivers/rtc/rtc-surface.c` + Kconfig/Makefile | Surface SSAM RTC | `RTC_DRV_SURFACE` |
| `drivers/hid/surface-hid/Kconfig` | 依赖改为 `SURFACE_AGGREGATOR_BUS` | — |
| `efi/libstub/fdt.c` + `sq2-el2-progress.h` | EL2 preboot：把最终 DT 拷贝地址交给 qebspil；`sq2.el2_trace=1` 时的屏幕进度标记 | `SQ2_EL2_PREBOOT_DESTINATION` |
| `remoteproc/qcom_q6v5_pas.c` | EL2 下改为 **attach** 已由 EFI 启动的 ADSP/CDSP/MPSS（不重载固件、不 assign 内存、不回写 handover） | `sq2_preboot_requested` |
| `remoteproc/remoteproc_sysfs.c` | 已配对 EFI 固件时 `firmware` 不再显示 `unknown` | `sq2_preboot_firmware_known` |
| `soc/qcom/rmtfs_mem.c` | 保留 EL1 已完成的 RMTFS 归属，不在 EL2 里做权限回收 | `microsoft,el2-preboot-rmtfs` |
| `iommu/arm/arm-smmu/arm-smmu.c` + `sq2-el2-context-limit.h` | EL2 实验档的 IRQ-backed CB 池（仅 DT opt-in + 精确硬件断言） | `SQ2_EL2_IRQ_BACKED_CONTEXT_POOL` |

### 3.4 判定"上游 6.18 已有"而**不再**移植的（有源码证据）

方法：把每个 6.3.8→项目 差异里**新增行**（≥25 字符）到 6.18 里统计命中率；命中率高说明是
上游回移，直接丢弃，避免把 6.18 的新代码"改回旧版"。

| 项目补丁 | 6.18 缺失率 | 结论 |
| --- | --- | --- |
| `adreno_gpu.c` zap `of_device_is_available` | — | 6.18 已是该写法 |
| `interconnect/sc8180x.c` CO0 keepalive | — | 6.18 已有 |
| `arm-smmu-qcom.c` SMR groups（上游 5583a55e074b） | 14% | 6.18 已含上游版本 |
| `usb/dwc3/*`、`usb/gadget/*`、`usb/mtu3` | 70–100% 命中 | 上游回移，丢弃 |
| `platform/surface/aggregator/*`、`tabletsw`、`surface-hid` | 41–93% 命中 | 上游回移，只保留 Kconfig 依赖调整 |
| `efivarfs`、`efi/vars`、`ucs2_strlcpy` | 50–100% 命中 | 上游回移，丢弃 |
| `sc8280xp.dtsi`、`sa8295p-adp.dts`、`sa8540p-ride.dts` | 54–100% 命中 | 与 SQ2 无关，丢弃 |
| `qcom_scm.c` QSEECOM（420 行） | 路径变为 `drivers/firmware/qcom/` | 6.18 已有 QSEECOM/UEFISECAPP；本项目配置两项均为 `not set`，未移植 |
| `gcc-sc8180x.c` 的 `GCC_USB3_UNIPHY_*_BCR` | — | 6.18 已有；仅补 clkref 时钟 |
| `gcc-sc8180x.c` PCIe GDSC `ALWAYS_ON` | — | 6.18 已改为 `PWRSTS_RET_ON` + genpd 同步下电，暂不恢复该历史 hack |
| `drivers/base/dd.c` `fw_devlink.timeout` | — | 6.18 已重做 deferred probe/fw_devlink 时序；需要时再按 6.18 语义实现 |
| `drivers/kernelsu` | — | 树外 KSU；本轮未接（见 §5） |

## 4. 与 6.3.8 的行为差异（必须实机确认）

1. **GPU**：6.18 用 `drm_gpuvm`/`drm_exec`、`drm_sched_init_args`、`a6xx_catalog`，且有 CX GDSC
   强制崩溃 + PDC 状态位 + 1s HFI 超时。也就是 `docs/KERNEL-6.18-FEASIBILITY.md` 里 A 路线
   想回移的修复，现在都是原生的。661/416 的 OPP 来自板级 DTB，未改。
2. **显示**：DP/DPU 是 6.18 的重写版（`dp_display.c`/`dp_link.c`/`dp_panel.c`），`dpu1` 只保留
   "禁用 UBWC 修饰符"这一处项目改动。**eDP 链路速率表（`msm-edp-rates`）尚未移植**。
3. **音频**：只保留"停播前静音"的机器级补丁；WSA881x 驱动保持 6.18 原版（与 `--machine-only` 语义一致）。
   6.18 的 ADSP 仍由 EFI preboot 启动，Linux 只 attach。
4. **USB/存储**：dwc3/gadget 全部换成 6.18 上游版本（旧回移已丢弃），NVMe/USB 网卡/USB 存储的行为
   需要在实机上重验。
5. **Surface 子系统**：SSAM/tabletsw/battery/AC 用 6.18 上游版本 + 项目的 Kconfig 依赖调整。

## 5. 尚未完成（明确列出，不得当作已修复）

1. **eDP 链路速率表 / 65Hz 入口**：`scripts/patch-sq2-edp-rates.py` 是按 6.3.8 的 `dp_panel.c`/`dp_ctrl.c`
   SHA 钉死的，6.18 的 DP 驱动结构已变（而且项目里 `msm-edp-rates.ko` 是"复制整份 msm 再打补丁"的做法）。
   需要按 6.18 的 `dp_panel.c`（`DP_SUPPORTED_LINK_RATES` 目前在 6.18 msm 里 0 引用）重新实现，
   或改为直接给 in-tree `msm.ko` 打补丁。**本轮未做。**
2. **TSENS**：6.18 主线的 `qcom_tsens` 仍要求 `NVMEM_QCOM_QFPROM`，而项目配置故意关闭 QFPROM；
   历史上用 linux-surface `b86f786` 源码单独编 `qcom_tsens.ko`。该源码归档本机 `downloads/` 里不存在，
   **本轮没有重建该模块**；6.18 的 TSENS 驱动把 `qcom,tsens-v2` 作为通用匹配（`data_tsens_v2`，
   `hw_id = i`），理论上可工作，但未实机验证。
3. **KernelSU Next**：`drivers/kernelsu` 是树外源码，本轮没有接入；`CONFIG_KSU*` 因此为未设置。
4. **IPA 4.1 / LTE**：`patches/sq2-ipa-v4.1-lab.patch` 针对 6.3.8 的 `drivers/net/ipa`，6.18 的 IPA
   结构又变了，**没有移植**。EFI FSG 采集 -> RAM RMTFS -> Radio 的路径本身在用户态/EFI 层未改，
   但内核 IPA 侧仍缺。
5. **Venus / QCA / USB 网卡等树外模块**：`CONFIG_VIDEO_QCOM_VENUS=m`、`SM_VIDEOCC_8150=m` 等按 6.18
   配置已编译通过；但项目里对 Venus system PM、QCA 失败清理、GENI TX 完成的**机器级补丁**未移植。
6. **休眠策略**：`suspend.screen_only` 隔断已移植；`nohibernate`、SPI/SSAM 唤醒、
   `patches/surface-pm/*`、`patches/sq1-suspend-wip/*` 未在 6.18 上重验。
7. **真机**：**完全没有启动过**。EL2 preboot 交接只在源码层面移植，qebspil 的 DT 属性路径
   （`/soc@0/remoteproc@17300000`、`@8300000`、`@4080000`、`/reserved-memory/memory@85500000`）
   已用 `dtc` 解出确认存在于新 DTB 中，但没有实机证据。

## 6. 下一步（真机 debug 顺序建议）

1. 组一个**独立 EFI 入口**（保留现有 6.3.8 默认/回退入口不动），payload = 新 Image + 新 DTB
   + 新 initramfs（模块目录按 `6.18.60-sq2-android1` 重排）+ 原有固件。
   现有 `scripts/build-source-boot.py`/`standard` 组包链是按 6.3.8 路径写的，需要先确认它如何取
   `Image`/DTB/模块，再决定是复用还是写一个 6.18 专用打包器。
2. 首启只看三件事：串口/屏幕是否进内核、`dmesg` 里 `msm` 是否 probe 成功（GMU 不再 watchog）、
   是否挂到 root。EL2 preboot 的 `microsoft,el2-preboot-complete` 是否被 qebspil 写入要在
   `/proc/device-tree` 里核对。
3. 起来后再按 §5 顺序补：eDP 速率 -> TSENS -> IPA/LTE -> KSU -> 树外模块。

## 7. 已部署的首启候选（2026-11-05 中间态，后续状态以 §0 为准）

### 7.1 ESP 整理（按用户指令"只保留 mesa dev 和 vulkan rollback，其他全删"）

删除 `/EFI/` 下 10 个实验目录：`SQ2Default`、`SQ2Free`、`SQ2GMUFix`、`SQ2LTE`、`SQ2Power`、
`SQ2U4`、`SQ2Vb3`、`SQ2Vp2`、`SQ2Vpm`、`SurfacePmAuto`（共释放约 1.48 GB）。
保留：`BOOT`（GRUB 主入口）、`SPX`、`SQ2Buttons`（EFI 引导器副本，2 MB，**没有当实验目录删**，
避免动到固件可能指向的引导路径）、`SQ2MesaDev`（默认入口）、`SQ2Fallback`（回退入口）。
整理后 ESP：2.0 G 用 531 MB（26%）。

### 7.2 新入口 `/EFI/SQ2Lts618`（手动选择，默认未改）

| 文件 | 大小 | sha256 |
| --- | --- | --- |
| `Image` | 34112000 | `ec54afb3…f838b49` |
| `android-el2.dtb` | 87709 | `e91bd896…87387d53e` |
| `initramfs.cpio.gz` | 199546708 | `73d93040…b198a7126` |
| `start.efi` / `el2launch.efi` / `qebspilaa64.efi` / `tcblaunch.exe` | 与原入口相同 | 从 `SQ2MesaDev` 原样复制 |
| `kernel-options.txt` | 449 | initrd 指向 `\EFI\SQ2Lts618\initramfs.cpio.gz`，其余选项与 MesaDev 一致 |

GRUB 追加菜单项（`/EFI/BOOT/grub.cfg`，原文件备份为 `grub.cfg.before-lts618-20261105`）：

```
menuentry 'SQ2 Linux 6.18.60 LTS (experimental)' --id sq2-lts618 {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Lts618/start.efi
}
```

`set default=sq2-default` 未改动，超时后仍进原 6.3.8/Mesa 入口；要试 6.18 需在菜单里选第三项。

### 7.3 initramfs 的做法（可复核）

没有重建 initramfs，而是**只替换模块树**：解压部署版（422.7 MB，5 个 cpio TRAILER），
逐个删除 `lib/modules/6.3.8-sq2-android1/**`（532 条）条目，在原位置插入
`lib/modules/6.18.60-sq2-android1/**`（478 条：143 目录 + 335 文件，含 `depmod` 生成的
`modules.dep/alias/builtin/order`），其余字节原样保留。

校验结果：**非模块条目 515 条，名称 + sha256 + 大小 + 权限逐条与原文件一致**；
TRAILER 数量 5→5；旧 release 残留 0；新树内 `msm.ko`、`spi-hid.ko`、`hci_uart.ko` 均存在。

注意：该 cpio 的 newc 头把 header+name 对齐到 4 字节（不是常见的 header+name+4-byte-pad），
直接按标准公式解析会整体偏移 1 字节。`out/_wsl/repack618c.py` 用的是修正后的偏移。

### 7.4 与工作配置对齐的一处 DT 修正

`zap-shader` 在 6.18 的 `sc8180x.dtsi` 里**没有节点**，而板级 DTS 只补 `memory-region`/
`firmware-name`，于是默认变成 `okay`；但本机没有签名的 zap 固件（rootfs 的 `lib/firmware/qcom`
只有 `a680_gmu.bin`/`a680_sqe.fw`），部署版 6.3.8 DTB 里该节点是 `status = "disabled"`。
已在板级 DTS 显式加 `status = "disabled"`，DTB 重新生成并重新上传。

### 7.5 首启已知差异（预期内，不是回归）

- **GPU 上限 514 MHz**：661/416 来自项目自己的 GPU profile DTB 合并步骤
  （`scripts/sq2-gpu-boot-payload.py`，其输入 DTB 是 6.3.8 时代的），本次用的是 6.18 直出的
  板级 DTB，`gpu_opp_table` 最高 `opp-514000000`。首启不追求频率，先看能否起来。
- **没有 `qcom_tsens.ko`**：`init` 里 `insmod /lib/modules/6.3.8-sq2-android1/.../qcom_tsens.ko`
  会失败，但随后的保护条件是"DT 存在 `opp-661000000` 才算失败"，新 DTB 没有该节点，所以不会
  触发 `fail gpu-tsens-module`。热区会缺失，AOSP 可能报 thermal HAL 症状。
- **不含** LTE/IPA、Venus 视频覆盖、KSU、closed Vulkan 的额外内核模块（这些模块在 initramfs
  的 `etc/sq2-*` 里仍是 6.3.8 ABI，`modprobe`/`insmod` 会被内核以 vermagic/ABI 拒绝，
  属预期失败，不影响 6.18 自带路径）。

### 7.6 回滚

默认入口与回退入口完全未改，直接选菜单第二项（或等 10 秒默认）即可回到 6.3.8。
彻底移除：`rm -rf /media/esp/EFI/SQ2Lts618`，并把
`/media/esp/EFI/BOOT/grub.cfg.before-lts618-20261105` 还原为 `grub.cfg`。

## 8. 限制声明

- 本文的"完成"只指 **`make Image modules dtbs` 通过**（0 error/0 warning），不是功能验收。
- 已部署且默认入口首次启动在 EFI 失败；用户从 6.3.8 回退恢复。本轮仅修正 DTB、未再重启，
  仍无 6.18 Linux 真机启动成功结果。
- 单机编译通过不能推出"任意 SQ1/SQ2 通用"；board DTS 仍是 SQ2 专用输入。
- 6.18 的 msm 是**代码更替**而非"补丁叠加"，GPU/显示/音频/LTE 的回归风险由实机验证承担。
