# SQ2 开发交接

**2026-10-30 带宽/降分辨率小对照：未测出带宽极限，已同boot恢复65**：用户先要求完整Android70测试。一次性入口实际启动88d5ec48…，5.4×4、DRM2880×1920@70/414750kHz、Android70、guardY均匹配，但用户明确画面/操作异常，不算通过；一次标记消费0，菜单恢复37bdce73…，用户手动恢复后adab52a4…实际65/Android就绪复核。没有自动恢复重启或热卸显示驱动。

用户随后提出2.7×4有效带宽假说，明确选择“固定70Hz降低真实输出分辨率”。已说明原生65按时序预算9.24288Gbps已高于8.64，不能从花屏直接认定硬门槛。新增软件EDID矩阵8c66f21b…，原生65保留首选，另列2560×1706@59.998135（283160kHz/预算6.79584Gbps）、同分辨率@69.999237（330360kHz/7.92864）、2700×1800@69.998662（366170kHz/8.78808）。只需一次矩阵候选启动；原init/base/Image/DTB/模块080cceff及凭据保持，原60/救援及默认65保留，无自动重启。Android cmd实机help支持临时STORE_MODE=false；get-active-display-mode虽列help但实际Unknown command，已改用dumpsys+真实DRM读回，未重复执行切换。

矩阵boot59556813-0cf8-4d11-90d7-056c6928ebc7，Android识别4模式，初始原生65。用set-user-preferred-display-mode 2560 1706 59.998135 0 false切真实KMS：Android2560×1706/59.998135、DRM2560×1706@60/283160、display ON、pixel读回283160000、链路540000×4；用户明确“2560×1706是黑屏”。此负对照预算6.79584<8.64仍失败，不能把黑屏当超2.7×4的带宽证据；本内屏非原生时序/视频传输配置混杂尚未排除，**带宽极限与70Hz最高可用分辨率未确定**。按控制门槛停止，后两个低分辨率70模式仅枚举、未执行，不原样继续逼近。

reset-user-preferred-display-mode 0 exit0，在同boot595…恢复DRM原生65/385120、Android64.99915、临时偏好null、guardY，用户确认“已恢复正常”。system_server/SF仍2222/2030，未重启框架或整机。已按盘身份/双UUID/root/hash/一次标记0校验，备份并原子恢复原5入口菜单37bdce73…，默认65/原60/Mesa/救援/关机保留；多模式仅当前启动驻留，下次默认仍单65。build-sq2-bandwidth-edid.py仅软件EDID、保持原生首选并替换3描述符为测试DTD，不写EEPROM；1项时序单测通过，原四角色control-v5复用，矩阵副本1/4/1模式、原生首选均64.999156、校验0、rollback65 hash/重建通过。out/display-refresh/bandwidth-test含方案/安装/控制失败/恢复证据，未入Git；新建status.json记录此失败控制与下一步约束，不自动重启或继续降分辨率。外部跳帧/物理链路吞吐仍未测。

**2026-10-30 默认65真实重启验证通过（短时）**：用户要求自动重启验证。第一次主动重启到4fb46847…，c754模块匹配/5.4Gbps×4/Android完成/用户确认桌面触摸正常，但实际DRM和Android仍60Hz，不能算65通过。已定位dp_drm.c对eDP不设置DRM_BRIDGE_OP_MODES，内置CONFIG_DRM_PANEL_EDP=y的panel_edp_get_modes直接读取EDID，跳过原MSM get_modes补丁；原源码主机测试不能证明该真实路径。该失败尝试及模块/源补丁保留为历史，不再作为有效65机制。

R2改用已启用CONFIG_DRM_LOAD_EDID_FIRMWARE的标准覆盖：只有默认65入口增加`drm.edid_firmware=eDP-1:edid/sq2-native65.bin`，不写屏幕EEPROM。以原128字节EDID14528fbf…生成软件副本，仅改DTD像素时钟、软件范围上限和校验，共偏移54/55/78/81/127；原生时序总量/极性及其他信息不动。EDID以10kHz表示时钟，385120kHz对应64.999156Hz（标称65），不是伪称精确385125。覆盖文件SHA83101509…，恢复已验证V6模块080cceff…，addon52767797…只改msm并新增EDID文件/父目录，原init/Image/DTB/base c5及凭据最后保持，无自动重启/回退钩子。

R2身份/hash校验、备份grub-before-default65-r2.cfg及原子替换后第二次主动重启到9fac091e-49cf-4015-bd2f-fa4321693745；实际cmdline/参数、固件文件与生效EDID均匹配83101509…，模块080cceff…匹配。DRM活动2880×1920@65、clock385120，链路540000×4，驱动像素读回385120000，Android默认/支持/渲染64.99915Hz，Android完成、SF/HWC running、guardY/nohibernate通过。约275秒复核同boot、system_server/SF PIDs2190/2022未变。新菜单SHA37bdce73bb6c96410579e5099da8adf87acba9614564bf1d935e24927dac1356，默认仍sq2-edp-default65；原四个60Hz/救援/关机入口无EDID覆盖且保留。本轮共两次明确主动验证重启，无自动循环；当前不再重启。**用户随后明确确认修正后这一轮画面、触摸和操作正常；外部不跳帧和长期稳定性仍未验证**。

有效源码/模块回到cached-only V6，原四角色同路径继续：MODIFIED_FILE.c/DIFF为实际部署V6（原错误get_modes为V7_DEFAULT65_UNREACHED补充归档），主机BASELINE/MODIFIED/ROLLBACK诊断AUX3/0/3、速率540000/index6，均exit0，恢复hash及补丁重建通过。default65-r2下EDID_BASELINE/sq2-native65/EDID_DIFF.json/EDID_ROLLBACK.sh/EDID_VERIFICATION扩展同一证据集，副本基线/修改/回退60/64.999156/60Hz校验和0、源hash恢复、逐字节重建通过；非三次物理启动。build-sq2-edp-edid65.py两项主机测试覆盖字节范围/保留原输入/拒绝覆盖及错误EDID，无私有原始EDID入Git。默认构建工具链并未全局启用65，仅此已识别设备的独立入口；未commit/push。

**2026-10-30 用户已要求5.4Gbps×4/65Hz设为默认，菜单已安装但新开机未验证**：用户随后明确尽量减少重启、可手动恢复，后续候选不含自动整机重启钩子。65Hz色条目视正常、130vblank/2.003758秒，原生回60成功，但显示服务恢复中SSH中断，之后415bc01d…原模块/Android复核；不能把此当完整65Hz桌面通过。无自动重启候选597ee8…保持原init，只替换V6模块。在5bbae357…单次接管中70Hz140vblank/2.000437秒/用户目视正常，75Hz150vblank/2.003712秒但用户报告异常；两档原生回60均成功。保留测试FB至Android接管时软件工具exit0、服务/活动60读回通过，但用户桌面仍异常，随后用户手动恢复，9bff4bf2…原msm f69c91e7…、guardY、Android完成复核。不能称桌面恢复修复或75Hz通过，不再自动重启。

最新默认65使用新独立模块c754e4142b796143cc92ddc02bb1ea25879d0029ef952663394fec0c30245798，源码7ec1c028…；只在SurfaceProX/eDP/本机LGD0555+LP129WT112684/至少5.4×4且原始完整60Hz时序匹配时，将get_modes的clock355500改385125并保留preferred。EDID/时序总量/极性/PHY参数不写改。标准drm_probe_helper在get_modes后重算crtcinfo，未放宽原依赖/导入检查，608导出闭包通过，无完整Module.symvers限制仍在。原base c5c64cda…不动；addon7cd97a1b…最终条目只改msm，init完全不变、密码SSH addon最后、无自动重启或联网/持续日志。

已按磁盘序列号/ESP marker/双UUID/root来源/guard及载荷hash校验，备份当前菜单grub-before-default65-manual.cfg后sync原子安装。实读默认项sq2-edp-default65，菜单完整SHA246fa9fdd86c558c9b83586849df7665369f14d220e36fd154f3189a439de9e6，五入口含新增默认65及原Qualcomm60/Mesa60/救援/关机，原四入口字节/行为不改；原始60菜单备份a7cc581f…保留。当前仍9bff4bf2…原模块正常60，**没有额外重启；完整Android65Hz开机、稳定性/跳帧待用户下次手动启动验证**。若异常手动选原Qualcomm入口，不热卸DRM/蓝牙或格式化。四角色仍control-v5同路径，最新MODIFIED/DIFF已更新为default65；真实get_modes主机BASELINE/MODIFIED/ROLLBACK clock355500/385125/355500及七种范围检查均exit0，恢复hash/patch重建通过。菜单副本默认项60/65/60、入口4/5/4及GRUB语法均exit0。out/display-refresh/default65保留安装/回读/审计；不提交私有镜像日志，本轮未commit/push。

**2026-10-30 eDP现机可行性测试与回退已完成**：独立worktree/原诊断分支，现机为另一块Toshiba128GB SSD及LGD_DV1.0面板，不能复用旧512GB固定身份安装器。保留其实际c5c64cda…默认镜像、Image/DTB/凭据与正常/Mesa/救援/关机四入口。V5限速2.7Gbps速率表对照实机BW_SET0/RATE_SET3/训练通过，用户确认正常；自动回退后原msm哈希复核。V6仅去除诊断AUX读，能力/训练读保留；源码7d87f794…/模块080cceff…/现机独立combined45bdb3e9…，608导出闭包和原依赖/last-entry-wins/密码addon最后通过，仍无完整Module.symvers验证。实机5.4Gbps×4、60Hz正常且用户确认，不能据此断言同一改动修复旧机V4。

原生KMS先跑60基线并恢复桌面，再90Hz：533250kHz、180vblank/2.005761秒、驱动时钟读回匹配；用户最初反馈异常，随后明确90Hz持续色条阶段正常，不仅是最后60回退帧。只算短时可行性，未集成Android90模式、未做外部跳帧/长期稳定性。120首次SSH失败且无runner目录，不计有效测试；独立重测eac96f1b…实际711000kHz、240vblank/2.003816秒，但用户持续异常，恢复60的KMS返回E2BIG。持久日志明确uptime93.99测试exit1、139.78独立75秒watchdog因缺恢复标记重启，非定时器提前打断成功测试。最终02596cb6…原msm f69c91e7…完整hash/Android完成/guardY/活动2880x1920@60复核，用户确认桌面恢复；原四入口菜单按身份/hash核验后sync原子恢复为a7cc581f…，默认c5镜像始终未改，无需额外重启。

新增patch-sq2-edp-cached-diag.py重现精确V6/拒绝二次应用；sq2-edp-mode-test.c是显式实验工具，非开机项；时序/参数主机ASan/UBSan测试通过，真实硬件结果独立记录。120原时序禁止原样重试；下一步若用户继续，先查E2BIG驱动拒绝位置/接收端时序约束，并为90补外部不跳帧及Android可恢复模式集成，不自动提升默认。证据在忽略的out/display-refresh，四角色仍control-v5同路径，最新源副本/补丁为cached-only V6，V5源/diff作为补充保留；主机BASELINE/MODIFIED/ROLLBACK诊断AUX调用3/0/3、速率均540000/index6、exit0、恢复hash及补丁重建通过。详见DISPLAY-REFRESH，原始日志/镜像不入Git，本轮未commit/push。

v4回退后补验：推送诊断分支后SSH已恢复，新boot ID、boot_completed=1、screen_only=Y、原msm完整哈希f69c91e7…均已核对，用户随后确认桌面/锁屏恢复，关闭下文“未复核回退”的缺口；v5仍未部署。

eDP v4诊断（2026-10-29）：初始5.4Gbps×4训练返回0，实读DPCD BW_SET=0、RATE_SET=6、lane状态77/77、对齐81，证明初始训练通过；不证明画面或高刷通过。PHY/OPP/像素时钟/视频ready调用均返回0，但vsync计数0/9/9/10/10停滞，后期AUX读取阻塞。已取回原始日志并主动sync+重启；用户最新反馈仍在启动，尚未复核本轮回退。v5仅本地构建：同一速率表路径限制2.7Gbps/index3、AUX仅前三次采样，待设备恢复后做对照，未部署。当前设备调试项仍v4，正常闭源/Mesa镜像未改。新分支codex/sq2-edp-training-diagnostics；详见DISPLAY-REFRESH。

**2026-10-30 独立干净编译并实机测试通过（未提升默认）**：用户要求全新编译并在本机测试。独立kernel-out从空目录编译Image/标准模块，补齐窄media基础模块配置后严格modpost通过，再编译当前Venus、C2和私有mapper。内核源为本机已打补丁归档（无.git，完整文件hash另存）；闭源Android/Qualcomm固件、已调优DTB及非视频专用外置模块复用，不能宣称全AOSP/闭源固件源码重编。保留screen_only、nohibernate、原SQ2触摸/音频/热管理等配置，不使用SQ1通用镜像。

产物out/clean-sq2-20261030：Image SHA12a9b126…，clean-initramfs.cpio.gz SHAca554ed4…/174836845B；ESP空间不足，因此部署10.85MB clean-addon.cpio.gz（f7a95828…），验证其叠加旧runtime+caps后与完整新包最终条目完全相同。私有SSH仍最后，未删旧镜像、未格式化。一次性入口sq2-clean-test，原默认sq2-c2-test不变，菜单备份grub.cfg.before-clean-20261030。

实机boot3150bf4f…确认新Image-clean-20261030，Android1/C2running/guardY/native-ready；新immutable运行目录/data/vendor/sq2-codec2/clean-20261030，service27161e41…、mappera64adec5…与产物匹配，v1不覆盖。四格式1080p60各600帧9979/9981/9978/9978ms，EOS/realtime/pass=true，STOP/RELEASE通过；VP9 SDR4K60既有约4.3Mbps向量600帧10095ms通过。普通APK查询四高通alias均在，VP8 4K60false/4K30与1080p60true。CPUmax1.7664/3.1488GHz、GPU661MHz、FUSE挂载、BAT1属性通过；桌面截图正常，不代替触摸/听音/长期稳定性。一次标记已消费0，下次正常重启回旧默认。HDR未测。

基线首轮在280输出时Surface取消并停止报错，用户随后确认不操作，独立重试四格式通过；错误日志保留。编译依赖与modpost纠正、objcopy改写ELF容器及历史mapper从原tar恢复的记录均在TEST_RESULTS.txt。四角色沿用vp8-limits，新增transaction仅补充菜单基线/修改/回滚副本验证，非三次硬件启动。此次构建/测试记录随本次main提交保存；私有构建产物和原始日志留在out，不纳入Git。

**2026-10-30 硬解收尾：重启回归通过，默认构建/部署已集成**：boot d9339db8…自动启动原生后端及AIDL服务，screen_only=Y，四个高通OEM alias均由普通APK发现。AVC/HEVC/VP8/VP9各600帧1080p60，输入/输出/渲染完整，EOS/realtime/pass=true，9977/9978/9978/9980ms，STOP/RELEASE通过。VP8新鲜查询1080p60/4K30=true、4K60=false；首次切用户后Activity暂未就绪的失败日志保留，未把旧结果当新结果。

当前SQ2只提升已实测runtime8b6c1f9a…+addonfb1831a9…的菜单默认项至sq2-c2-test；原正常/原Mesa入口及镜像全部保留。已核对BC711容量、ESP B027-0512、data UUID、marker、原菜单和载荷hash，sync原子替换；新菜单SHA e1fe29c57b99f8bf8d5502ee8c63b2ee5e241eda7c5bca0eafc44dc950eb4e1f，备份grub.cfg.before-default-video。一次标记已消费0，提升后未额外重启；运行的正是此前正常重启通过的同一组合。没有将含不同SQ1触摸策略的通用f749镜像覆盖当前SQ2。

build-source-boot默认包含硬解；build-source-disk生成正常启用/救援禁用菜单并在manifest写入hardware_video策略和菜单hash。deploy-experimental-usb在磁盘选择之前读取镜像内真实菜单校验，可用--require-hardware-video拒绝旧镜像；旧build-googlebook-ssd --update-boot拒绝硬解manifest，防止退回旧菜单。USB身份/原生NVMe拒绝保持。默认完整boot构建通过；本轮没有重新制作/烧录整盘镜像，公共无私有二进制的完全可复现构建仍非本次交付。

主机部署4/default-menu3/XML2/runtime-boot3/runtime-package3、OEM别名、allocator标签测试通过；P010 copy和V4L2 session ASan/UBSan测试通过（初次缺host g++，安装后重跑exit0）。证据沿用out/video-decode/vp8-limits四角色及reboot-capabilities-fresh.txt、sq2-caps-addon-boot-play-d933-r2、promote-default-result.txt。BASELINE/MODIFIED/ROLLBACK的VP8 4K60=true/false/true，最终false。Bilibili剩余权益问题用户已定位；HDR继续暂停，不宣称任意高码率/长期稳定性。此记录随硬解收尾提交，音频未跟踪文件不纳入；Git合并结果以实际提交记录为准。

**2026-10-30 默认源码构建已纳入硬解，部署/合并未完成**：新增scripts/sq2-video-boot-payload.py固定读取已审计12f1c0d5…候选，白名单导入etc/sq2-video-native、etc/sq2-c2-runtime-boot和固定hash Venus固件，helper从当前源码读取；不导入别的init/凭据。build-source-boot.py默认集成早期stage及post-boot后台hook，新增--output防覆盖。实际完整构建out/video-decode/default-build/native-combined-initramfs.cpio.gz SHA f749c61c7f7b28173497cc211ff44a4cffe1b480d26515fea6ff99bc619883b8，审计hardware_decode_included=true/password_addon_last=true/hardware_boot_tested=false。

build-source-disk.py加入视频载荷/源码XML前置校验，生成grub-hardware-video.cfg（不再直接使用旧未跟踪菜单）；正常入口自动flags、救援不启用，三个启动helper新增sq2.video=0优先退出。default_menu三测试/runtime_boot三组/boot_codec_xml两测试和diff-check通过。尚未生成新整盘镜像或部署。注意通用源码安装流程原有ghost_touch_fix=False等非视频配置属于另一路SQ1安装工作，不能将f749完整镜像直接覆盖当前SQ2而回退其触摸等已验证修复；当前SQ2应基于其现用已审计镜像仅更新视频载荷/开关来做重启回归。

下一步补部署清单对默认硬解菜单/载荷的一致性校验及实测，再以当前SQ2原非视频状态保留的独立候选启动验证VP8能力/四格式自动播放/高通别名，必要正常入口提升并保留救援；完成后审查硬解文件单独提交、合并main。当前仍未commit/merge/push，不混入音频文件。四角色沿用vp8-limits并延续ledger；原Android BASELINE4K60true/MODIFIEDfalse/ROLLBACKtrue已完成，不重复固件测试替代缺失部署验证。

**2026-10-30 用户新增交付要求：默认构建/部署、提交并合并主分支**：完成后仅提交硬解相关改动并合并main；独立音频未跟踪文件保持，不混入私有产物。已git fetch origin；当前codex/sq2-video-decode HEAD81b72a6，origin/main9f4f426，尚未提交/合并。用户新要求取代仅修VP8后收尾的旧边界，默认构建与部署验证也是完成条件。

已将实机验证VP8 XML候选整合configs/sq2-media-codecs-c2.xml；pack-sq2-c2-runtime-boot.py新增boot_codec_xml读取源码配置并固定校验5bfe57fa…，顶层boot XML独立于旧de2d runtime.tar，保持/data/vendor/sq2-codec2/v1不可变文件与hash不变。新tests/test_sq2_boot_codec_xml.py两项校验/拒绝测试、runtime_boot三组、codec_names及git diff --check通过。独立镜像out/video-decode/vp8-limits/boot-vp8-limits.cpio.gz SHA12f1c0d506d5dc654461e07bba96869ad601bf493e2b56d1ffc3ad3c64a299fc打包审计通过，私有SSH仍最后，尚未上传/启动。

下一步将运行包/原生模块/固件与早期注册、post-boot自动启动复用进scripts/build-source-boot.py默认路径，更新build-source-disk.py菜单生成/部署输入检查，正常入口启用、救援入口不启用，并保留opt-out；不削弱USB/NVMe磁盘身份保护。注意源码启动包目前源base651f…，不能简单假设包含实验native bundle；lab-start依赖etc/sq2-video-native清单及venus-5.2/venus.mbn。现build-source-disk.py直接使用out/source-boot/grub.cfg，需避免未跟踪旧菜单漏掉硬解开关。之后构建完整候选、设备安全部署/重启查询和播放回归，再提交合并；不要提前标完成。

**2026-10-30 VP8联合限制真实Android查询通过，尚未持久化**：先确认boot c1c87f3e/guardY、media PID2321、资源管理器无活动codec、设备XML原SHA1e9c4b56…。运行out/video-decode/vp8-limits/device-check.sh仅替换vendor RAM overlay的media_codecs_c2.xml并重启init服务media（不是C2/native/PAS），同一普通capsquery APK执行BASELINE/MODIFIED/ROLLBACK/FINAL：VP8 3840x2160@60依次true/false/true/false，1080p60和4K30全部true，rollback原hash和final候选5bfe57fa…hash通过。整个脚本exit0、boot不变、guardY、C2服务running，未重启固件。四阶段结果和原XML已取回vp8-limits/sq2-vp8-limits-c1c8，四角色VERIFICATION追加字面日志。当前设备overlay保留候选；本机原configs及启动镜像未改，重启会丢失修正，不能标完成。下一步只将已验证XML整合独立启动候选并验证持久化，保留原runtime二进制与userdata不可变包，不修改其清单造成安装拒绝；现pack-sq2-c2-runtime-boot.py从旧runtime.tar取XML，需要有明确且验证过的顶层XML覆盖路径。然后按用户最新收尾边界完成，不扩展HDR/新格式。

**2026-10-30 用户确认账号权益冻结，收尾边界更新**：Bilibili剩余高画质码流问题由用户确认账号权益暂时冻结；停止该方向请求/响应追踪，不绕过权益。此前4K菜单缓存修复及应用自行重检测true保留。仅本机准备过分析依赖，未安装/运行设备注入服务。用户随后表示“解决后已经可以视为完成”：按当前剩余VP8联合能力误报修正并验证后收尾，不自动扩展新格式/8K/HDR专项；不能将尚未部署的候选算完成。

VP8 XML候选out/video-decode/vp8-limits/MODIFIED_FILE.xml仅为原配置副本，增加16x16 block-size、block-count1-36864、blocks-per-second1-1036800、frame-rate1-240，原configs/sq2-media-codecs-c2.xml未改，其他codec/高通别名/HDR未改。来源caps-bcd0.json完整解析中VP8 decoder capabilities，不使用encoder值。初版证据脚本误读其他property的entries导致KeyError max/exit1，已限定capabilities后修正。同一check.py主机验证BASELINE/ROLLBACK缺联合限值exit1，MODIFIED边界算术exit0，4K30/32允许、33/60拒绝，1080p60允许；rollback hash及patch重建一致。四角色同目录DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh均回读。仅XML算术验证，尚非Android解析或实机播放验证。下一步验证Android读取候选后VP8 4K60=false且4K30/1080p60=true，再整合部署/持久化；现runtime/native不重跑PAS、不卸载。

**2026-10-30 HD剩余4K播放问题已缩小到解码前码流选择**：回读13:03切换日志，SetUserQn=120，但IJK收到的HEVC列表仅quality80/64/32/16，streaminfo videos=4/cur_video_id80，输出size1920x1080；并未看到4K码流进入解码器。13:06正常VIEW重新进入同BV，play view request success后仍同四档，自动选64/1280x720。不能用这轮推断4K硬解失败，也不能仅凭IJK规范化列表断言服务器没有返回4K。APK PlayViewRepository通过q13.a构造fnval，后者静态代码在isSupport4K时OR128；单类反编译部分不完整，不代表实际请求bits已确认。下一步区分原始PlayView返回与客户端过滤，限采集qn/fnval/codec/尺寸等元数据，不提取账号密钥/绕过权限。boot c1c87f3e及guardY保持，本轮未改设备配置；证据cache-refresh/stream-selection.txt、reenter-log.txt及延续VERIFICATION.txt。

**2026-10-30 Bilibili HD 4K菜单恢复，实际4K播放未确认**：tv.danmaku.bilibilihd 2.0.1/2001100，BV1NGZtBwELa同机Chrome显示4K而HD最高1080P高码率。已安装APK静态检查定位IjkOptionsHelper.isSupport4K：已有pref_key_is_ijkplayer_support_4k时永久复用BLKV缓存，只有缺键才检测HEVC 3840x2160/6Mbps。设备缓存该键false、HEVC enable最新true；普通APK能力查询HEVC4K为true且HD实际使用硬解。备份原75c4583b…到/data/local/tmp/sq2-bili-4k-cache-backup-c1c8/baseline.blkv，仅将这一键等长重命名为old__key…使缓存失效，保留其他键/账号/下载；不是强制写true。停止并重开HD后应用自行重新检测写true，截图确认4K超高清选项恢复。boot c1c87f3e/guardY，未重载固件、改启动盘或测试HDR。

证据out/video-decode/bilibilihd/DIAGNOSIS.txt及cache-refresh四角色MODIFIED_FILE.blkv/DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh；同一文件检查BASELINE=false/MODIFIED=MISSING_REPROBE/ROLLBACK=false均exit0，rollback hash一致，binary patch重建通过；这是副本测试不是三次实机播放。实际重检测REPROBED.blkv=true。随后点4K，日志请求qn120，但后续截图仍1080P、视频size1920x1080，伴网络-1001/timeouts；不能称4K播放通过，尚未隔离请求/流选择/自适应回退/解码重配环节。PID5794 UID1010250资源名c2.sq2.hevc.experimental，播放器遥测名称OMX.qcom.video.decoder.hevc。下一步追踪qn120实际流与回退原因，再继续联合能力限制和完整SDR适配；保留当前runtime，不重跑PAS。原始APK/账号相关日志/BLKV只留out不入Git。

**2026-10-30 runtime-v1 VP9 4K60 SDR普通应用通过**：同boot c1c87f3e/guardY，直接使用启动镜像自动运行的服务/native，不临时替换任何后端或声明。新增独立sdrnames.vp9fourk APK（UID1010261），FFprobe实际Profile0/yuv420p/3840x2160/60fps/600帧，容器平均码率4303921bps（不是高码率压力流）。普通APP报告format_supported/hardware=true/software=false，自动选c2.sq2.vp9.experimental，输出3840x2160，600输入/输出/渲染、EOS/realtime/pass true、elapsed10112ms（沿用10秒+500ms门槛），STOP_OK/RELEASE_OK。

600帧copy_sync2625536us（DMA_SYNC784940us），IMPORT_RELEASE4/50184192B、SESSION_CLOSED0；boot不变、服务继续running、未重新PAS。证据out/video-decode/vp9-4k60-runtime（fixture/APK、sq2-vp9-4k60-runtime-c1c8结果/logcat/截图、timing.txt）。此结果补齐VP9 profile0当前测试流的4K60组合，不能推出任意码率/分辨率/长期稳定性；VP8高分辨率、联合限制及未实现格式仍需继续。正常默认入口未提升，HDR继续跳过。

**2026-10-30 runtime-v1一次性启动与四格式自动播放通过**：重新核对BC711/1000215216、ESP B027-0512/marker、data UUID4f46…及loop0/p3，ESP余323916KiB足够199149026B候选。上传独立native-c2-runtime-v1.cpio.gz.new，设备SHA8b6c1f9a…/gzip通过后sync原子改名。旧菜单/env备份grub.cfg.before-runtime-v1、sq2-c2-test.env.before-runtime-v1；仅实验入口换新initrd并加sq2.video.autostart=1，菜单SHA28ab2b494d5fc5f7e0750162b5d229ee08d737ff6a7a23cf002c8b9411f200f8，默认sq2-internal保持。once1校验d762后原子设置并正常reboot；初次SSH超时后再次检查恢复，没有重复重启。

新boot c1c87f3e-d69d-4e8a-9d33-e40e4f867a5b、guardY/boot_completed1、实验服务running。早期stage全部校验OK且DATA_UNTOUCHED，后台自动lab-start清单/固件校验通过、SQ2_NATIVE_VIDEO_READY、/dev/video0存在、PAS marker为新boot；未手动装载模块/替换服务。一次标记消费为0。切换user10/唤醒后直接运行既有四APK，均hardware=true/software=false/format_supported=true、自动选中对应C2、600输入输出渲染、EOS/realtime/pass true、STOP_OK/RELEASE_OK，AVC/HEVC/VP8/VP9耗时9979/9979/9979/9976ms。此次测试不再创建/撤销临时媒体XML，也不替换运行服务；验证的是镜像自带路径。

证据out/video-decode/runtime-v1：deploy.txt、boot-stage.txt、auto-native.txt、sq2-runtime-v1-boot-play-c1c8。补充四角色MODIFIED_FILE.cfg/DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh对菜单副本执行同语义检查，BASELINE autostart=False/MODIFIED True/ROLLBACK False均exit0，patch重建及恢复hash一致；不是三次实机启动。原qcom-names四角色保留。默认正常镜像尚未提升，当前已启动的实验服务/native保持运行；不要重跑PAS。下一步检查正常启动提升的范围与回退并继续能力联合约束；完整能力和长期稳定性仍未完成，HDR跳过。

**2026-10-30 runtime-v1独立启动候选已打包，未上传/启动**：实查实验initramfs，早期video-codec-manifest-stage发生于userdata尚未挂载时，不能在此写/data。设备init.rc确认post-fs-data先于on boot/class_start hal。新增video-runtime-stage只在vendor RAM overlay布置不可变运行包/安装器、原AIDL default1 rc/VINTF及媒体XML；vendor wrapper在HAL启动时先运行安装器，要求实际/data ext4挂载及guardY，校验镜像内固定hash，已有v1必须逐项匹配不可变manifest/拒绝符号链接，不覆盖未知版本；缺失才暂存校验后原子改名。无格式化/主动挂载userdata。

新增video-runtime-framework-boot在retained initramfs后台等待Android boot_completed和服务running，只有experimental及新增sq2.video.autostart=1同时存在才调用既有lab-start；rescue/no flag直接退出，once目录和既有PAS guard保留，不卸载/重试固件。未增加网络、密码、休眠或全局mapper变化。pack-sq2-c2-runtime-boot.py以已审计e519实验镜像+de2d运行包为固定输入，生成out/video-decode/runtime-v1/boot-v1.cpio.gz，SHA8b6c1f9a83aee85cf86aca4342147ee50bd43fb37e17d8f3a9750a759761484b。完整解压last-entry-wins审计仅13条运行包/两个helper/init变化，其余内容保留，私有密码SSH addon仍最后；含凭据的镜像保持out，不提交。

tests/test_sq2_runtime_boot.py三组主机测试通过，实际执行安装器fresh/existing、guardN/未挂载/损坏tar/篡改既有版本/父目录symlink拒绝，以及启动helper缺flag/rescue无操作。非真实启动证明，early-stage和首次启动时序仍待设备验证。本轮没有写ESP、重启或PAS。下一步重新核对设备磁盘/ESP/当前菜单与空间，上传独立候选，实验入口添加autostart旗标并保持默认入口/回退，再一次性正常重启验证自动注册/native启动/普通APK；不能把打包通过记为重启通过。

**2026-10-30 版本化SDR运行包已实机验证，未接入开机**：新增pack-sq2-c2-runtime.py及sq2-c2-runtime-launch.c，将已测owner27161e41fb7aaba13a851ee14d9168da75568dc98b3ec21e2a14db954c602ee5、私有缓存库eb31d402…、四格式高通Alias XML和启动器打成独立tar。输入服务/库固定SHA拒绝未知版本，拒绝覆盖输出，逐项回读归档；两次构建得到相同SHA de2d825b34e09158076cf6b14f5777d0f843d6f5cc782d3961d7b2a6531b7f50。三项主机拒绝测试通过。包不含密钥/密码，不操作设备、VINTF或固件。

设备root专用/data/vendor/sq2-codec2/v1已暂存此包（父目录0700），start.sh每次校验全部载荷、guardY，启动器建立私有mount namespace、绑定包内mapper、设置MAP_CACHE=1、清除P010_DIRECT/LD_PRELOAD并exec包内service。--names-test实机8名称通过/4secure拒绝。随后同boot50ec已有native，用包内路径临时启动服务/提供XML，VP9普通APK自动选中硬件，1080p60/600输入输出渲染/9978ms/EOS/realtime/pass true、STOP_OK/RELEASE_OK；mountinfo确认来自版本化目录。原service4e7d和全局mapper c1a57082恢复/保持，XML_REMOVED，无PAS重载。

产物out/video-decode/runtime-v1/runtime.tar及同目录设备检查、播放证据；原qcom-names四角色ledger继续扩展，原host BASELINE1/MODIFIED0/ROLLBACK1不变。该包解决运行时依赖散落在临时测试文件的问题，但未修改initramfs/默认启动：下一步需把版本化运行包与早期VINTF/媒体声明、一次性native启动时序整合为新的独立启动候选并审计回滚，不能直接宣称重启后可用。现有v1目录已存在，后续必须验证/复用或换新版本，不能盲目覆盖。

**2026-10-30 四格式自动选择及HEVC一分钟循环通过**：同boot50ec、已驻留native，未重载固件。复用前轮四个普通APK/1080p60/8-bit素材，改codec=auto：AVC High、HEVC Main、VP8、VP9均报告hardware=true/software=false/format_supported=true，AUTO_SELECTED分别为对应c2.sq2.*.experimental；各600输入/输出/渲染、EOS/realtime/pass true、STOP_OK/RELEASE_OK，耗时9978/9980/9977/9979ms。显式高通别名和自动选择两条路径均有证据，但自动选择返回Codec2 canonical名，不谎称已将canonical改成OMX。

随后同会话HEVC Main以loops6重复10秒片段，3600输入/输出/渲染、59980ms、EOS/realtime/pass true，STOP_OK/RELEASE_OK，原实验服务4e7d98a7…恢复、全局mapper c1a57082…未变、XML_REMOVED、guardY。此为60秒循环/时间戳拼接验证，不是独立长片、小时级稳定性或多会话压力认证。脚本按严格结果断言退出，未放宽门槛。证据out/video-decode/qcom-sdr-matrix/sq2-qcom-auto-matrix-50ec、sq2-qcom-hevc-loop-50ec和auto-kernel.log/loop-final.txt；原qcom-names四角色ledger延续，主机名称BASELINE1/MODIFIED0/ROLLBACK1保持。接下来需补可重复部署/正常启动持久化及联合能力边界，不再重复已通过的同一短片检查充当完整适配。

**2026-10-30 四种高通原版名称的普通应用1080p60验证已齐**：同boot50ec/native驻留/guardY，不重载固件。新增独立包sdrnames.avc/hevc/vp8，实际UID1010258/1010259/1010260；FFprobe逐帧核验素材均1920x1080/60fps/yuv420p/600帧，分别AVC High、HEVC Main、VP8 profile0，签名验证及user10安装成功，保留原包和数据。首轮WSL内联shell变量传递丢失导致意外生成默认baseline素材，后续期望High文件不存在的ffprobe exit1；已改用保存的Python subprocess参数数组逐项构建，错误素材未用于本轮测试。

collector8621确认READY、独立脚本8915复用私有eb31映射缓存和新名称owner，三个普通APK显式使用OMX.qcom.video.decoder.avc/hevc/vp8：各600输入/输出/渲染、EOS/realtime/pass均true，分别9978/9979/9979ms，STOP_OK/RELEASE_OK、三个CODEC_PASS及TESTS_FINISHED。结合上轮VP9 9981ms，四种原版名称均已完成普通UID的1080p60测试APK播放；不是所有第三方APP、4K边界、长片或完整生命周期认证。恢复原service4e7d98a7…、全局mapper c1a57082…不变、XML_REMOVED，native仍驻留，禁止重复PAS。

证据out/video-decode/qcom-sdr-matrix：三个fixture.json/APK、sq2-qcom-sdr-matrix-50ec中的结果/logcat/截图及final-state。原qcom-names四角色保留并扩展ledger，主机名称BASELINE1/MODIFIED0/ROLLBACK1不变。后续优先将已验证服务/私有启动器/声明纳入可重复实验部署，补App枚举/自动选择、长播放和联合能力边界；正常启动持久化仍未完成，HDR继续跳过。

**2026-10-30 VP9高通名1080p60实时通过（私有映射缓存对照）**：同boot50ec/guardY/native驻留，不再PAS。重新核对私有库eb31d4022c97e1ac70e4b396eeb7c5599d1f904730bbdd241704a6929bc967a8与历史已测缓存版本一致，全局库c1a57082…未改。仅复制现有sq2-map-cache-private.c并将exec目标指向已验证的新命名owner，NDK构建私有启动器；不改变缓存算法、预算或同步。独立collector26307确认READY后运行相同APK/1920x1080 VP9 profile0 60fps码流/高通名称，服务26575 mountinfo确认私有bind而非全局替换。

普通UID1010257以OMX.qcom.video.decoder.vp9显式播放：600输入/输出/渲染，EOS/realtime/pass均true、9981ms、STOP_OK/RELEASE_OK。与上一轮无缓存13984ms失败保持同一门槛。600帧copy_sync由8756477us降到662502us，其中DMA_SYNC193052us、Y319645us/UV148325us；IMPORT_RELEASE count7/22020096B、SESSION_CLOSED result0。不是“零复制”，也不证明4K60/所有APP/其它codec。全局库原hash、原实验服务4e7d98a7…和临时XML清理均核验，未重启、未重载固件。证据out/video-decode/qcom-vp9-app/sq2-qcom-vp9-cache-50ec及cache-timing.txt，源原件未改。下一步把已验证私有启动条件纳入可重复部署，并依次验证AVC/HEVC/VP8高通名称的普通应用播放及更高尺寸/长播放；HDR继续跳过。

**2026-10-30 高通VP9名称普通应用真实播放：功能完成，1080p60实时性失败**：同boot50ec46b7、guardY，独立包org.surface.sq2.videotest.sdrnames安装user10，保留原测试包/数据；素材为既有VP9 profile0/yuv420p/1920x1080/60fps/600帧。独立collector19347 READY及smoke确认后本boot仅一次lab-start成功（core a5f2bcc5…/dec66e9c26a…），没有重载固件。data wrapper新命名服务+临时XML窗口，普通UID1010257显式CREATE OMX.qcom.video.decoder.vp9并START同名，实际输出1920x1080，600输入/输出/渲染、EOS true、STOP_OK/RELEASE_OK；elapsed13984ms、realtime=false、pass=false，不得记1080p60通过。截图已查看，完整彩条测试图，不代替逐像素验证。

OUTPUT_TIMING frames600：fetch694203us/map88905us/copy_sync8756477us/unmap42821us；PIXELS y5739553us/uv2875018us，输出复制是主要已观察开销。此窗口未使用旧私有map-cache wrapper，只用了data可执行路径；不能把既有4K优化结果直接移植为本轮性能结论。下一步核对既有缓存映射候选与当前后端CPU访问属性，再做同码流对照，仍保留600帧/实时门槛。原实验service4e7d98a7…恢复、临时XML移除、boot不变，native当前驻留，禁止再次lab-start/PAS。证据out/video-decode/qcom-vp9-app（stages/result/logcat/frame/native/kernel/final-state），原qcom-names四角色ledger已追加；主机名称基线1/修改0/回滚1不变。其余三个高通名称普通应用播放尚待验证，HDR仍跳过。

**2026-10-30 高通别名已通过真实MediaCodec创建/释放，尚未播放**：重新核对磁盘/ESP/菜单及镜像后，备份一次env到sq2-c2-test.env.before-sdr-names，原子设置once1；首条重启因PowerShell末尾CR导致路径reboot\r不存在（exit1），随后纠正命令正常重启，非自主崩溃。新boot50ec46b7-22ac-4efc-ae2a-853d5596237a、guardY、boot_completed1、实验AIDL服务running，一次标记已消费为0。本boot没有执行lab-start/PAS，也没有视频节点。

名称窗口a把新ELF直接放vendor，因linker namespace缺libicu.so服务停止，创建超时124；主动终止脚本并恢复原service hash4e7d98a7cbb4621cfada47bacd9f75250d542aab40d1543df369d532ff640b89。窗口b纠正为vendor shell wrapper执行data中的同一候选，并在测试前确认服务running；原XML四别名未改。真实AMediaCodec_createCodecByName对AVC/HEVC/VP8/VP9的c2.sq2旧名和OMX.qcom原版名共8项均nonnull、delete0、exit0；logcat对应8次CCodec分配正确canonical组件，非旧OEM OMX实现。无configure/start/视频帧，尚不证明普通UID应用播放、MediaCodecList别名展示或吞吐。两轮都持久日志及trap恢复，窗口b收尾原service hash一致、XML_REMOVED、PAS_NOT_ATTEMPTED、guardY，boot不变。

证据out/video-decode/qcom-names/framework-a.txt、framework-b.txt、framework-b-logcat.txt、final-state.txt；原四角色已重新打开，VERIFICATION.txt追加本轮证据，原host BASELINE1/MODIFIED0/ROLLBACK1不变。下一步复用此boot实验注册环境，先独立collector再仅一次lab-start，使用data wrapper新服务做显式高通名的普通APK SDR播放；不可重复走vendor ELF链接失败路径，不启用P010专项。

**2026-10-30 高通原版名称兼容候选**：用户要求所有已验证解码器名称与原版高通一致。实查设备vendor XML的非secure AVC/HEVC/VP8/VP9名称分别为OMX.qcom.video.decoder.avc/hevc/vp8/vp9。现有实验XML已列出四个Alias，但store此前只接受c2.sq2.*。现sq2-codecs.h增加sq2CodecAlias/sq2CodecMatches，store的Traits.aliases、createInterface、createComponent均接受对应高通名；返回接口保留Codec2 canonical名及旧测试名兼容。不新增secure/MPEG/HDR能力，不把此自研后端称为OEM OMX实现。

服务构建通过；设备独立--names-test验证四组共8个名称均能创建interface/component，4个secure名称拒绝，NAMES_PASS/exit0，无Binder注册、无固件启动、无视频解码。尚未验证MediaCodecList公开别名、createByCodecName全链路及不同APP，不能称部署完成。tests/test_sq2_codec_names.py从现有XML读取别名，交叉编译实际header解析逻辑，覆盖旧主名、别名、错codec和secure拒绝；同测试BASELINE1/MODIFIED0/ROLLBACK1，patch重建及还原字节一致。补充四角色out/video-decode/qcom-names：MODIFIED_FILE.tar、DIFF_FILE.patch、VERIFICATION.txt、ROLLBACK.sh（目标为包含两个源文件的副本目录）；原角色保留。下一步在已审计实验入口使用新service与既有XML，验证Android名称发现和显式高通名播放，再继续SDR吞吐。

**2026-10-30 用户更新目标：跳过 HDR 相关能力**：后续硬解适配继续推进 SDR，不再推进 HDR/HDR10、P010直接输出及Main10性能专项；已有实现、失败证据和回滚保留，不删除或宣称修复。10-bit与HDR不等价，本次暂停专项不代表修改已存在的profile声明或硬件能力。此条优先于下方历史记录的P010“下一步”。

**SDR接续启动审计与runner已完成**：只读重新核对BC711/1000215216、ESP B027-0512/marker SQ2-INTERNAL-512-20261027、data UUID4f46d2ec-323e-486e-b473-fc2adc0849ba、loop0 backing p3及一次env=0。设备菜单d1ae9af6…未变；取回正常镜像5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c及实验镜像e519556b0d5e5aa7ccb98772ef58fceee6fad2647f905cf85a914cab58a60b7b，完整hash/gzip及last-entry-wins比较成功。最终差异仅23个新增视频目录/文件和init的一处video-codec-manifest-stage调用，其余正常功能（含私有SSH文件）不变。证据out/video-decode/sdr-boot-audit/comparison.json；镜像仅私有out，不提交。没有写ESP、没有重启、没有PAS。

原test-c2-player.sh保持不变，副本out/video-decode/sdr-vp9-1080p60/MODIFIED_FILE.sh新增vp9-1080p60允许标签及600帧实时/EOS验收分支，不把它套用300帧判定。该目录DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh为补充四角色，原venus-transport及p010-backpressure角色保留；同sh -n检查BASELINE/MODIFIED/ROLLBACK均0、patch重建和还原hash一致，非硬件解码验证。下一可执行步骤：再次确认设备当前boot/一次env后，备份并原子设置既有一次性实验入口，再正常重启进入已审计镜像；启动后先核对模块hash、guard及独立collector，之后每boot仅一次lab-start。复用NV12/原allocator，不恢复P010专项。APK仍未安装，须先核对签名或沿用独立包名，不能卸载原应用数据。

本轮只读实查设备仍为5e25567a-6282-42ac-a81e-d9e5c01773b3、screen_only=Y、nohibernate，正常启动未含sq2.video.c2=experimental，无video节点及实验C2服务状态；未重新启动固件或改启动盘。SDR接续先补VP8/VP9高分辨率与吞吐验证（既有普通APK证据仅320x240），随后覆盖生命周期/长播放及正常启动集成；不能把AVC/HEVC通过替代VPx验证。MPEG和联合尺寸/帧率约束仍未完成。已用现有构建器生成VP9 SDR APK，ffprobe完整计帧确认Profile 0/yuv420p/1920x1080/60fps/600帧，签名验证通过；SHA256为88c08757af6bbc178423204a9624488dc04d1ca05970f6255f76a971fa60160b，产物out/video-decode/player-app-1920x1080-vp9-60fps/sq2-video-test.apk。尚未安装或实机播放，不代表硬件吞吐通过。

**2026-10-30 直接输出背压离线修复候选，未实机**：按固定frameworks/av源码核对C2IgbaBlockPool与C2BufferQueueBlockPool，未可用槽/等待fence可返回C2_BLOCKING，不能统一当物理ENOMEM。组件现在C2_BLOCKING映射-EAGAIN、C2_NO_MEMORY保留-ENOMEM、其余-EIO，并保留原始C2状态日志；这修复错误分类，但上轮真实C2状态未记录，不能反推六帧失败必为BLOCKING。

sq2-v4l2-session.h新增refill_逐槽状态：DQ交付后不复用客户端owner；分配新lease用局部临时对象，失败不污染槽；EAGAIN保留空槽，后续每次pump重试一次，不重复交付、不自旋、不重排入队客户端像素。QBUF成功才清空refill；收到空槽的重复DQ返回EIO；close清空待补槽。NV12固定复用模式保留，真正ENOMEM仍sticky失败，LAST/EOS不要求补回已结束槽。首次configureCapture分配遇阻塞仍fail-fast；组件尚无独立输出事件线程、长阻塞调度/outputDelay联合协商未完成，不能宣称完整背压已解决。

扩展实际头的syscall mock：NV12/P010暂阻3轮后恢复、持续阻塞时close、EAGAIN后真实ENOMEM、空槽重复DQ、阻塞时EOS、客户端保留lease及释放计数均通过，旧固定100帧/4K池预算/部分分配回收也通过。四角色out/video-decode/p010-backpressure：同新增测试、同无参数输入，BASELINE/ROLLBACK在暂阻断言处SIGABRT（Python returncode -6），MODIFIED0；patch重建和rollback源hash一致。禁core输出，不是实机三态。默认服务构建SHA55099296565fc4b731827c3b683a32f3785575c23668ae1321ec5a6b6ecb6889，未部署。本轮未操作设备、未启动固件。下一步在重新核对当前启动入口/设备状态后，通过原始C2状态与池额度日志验证直接输出，仍保留原allocator及row owner恢复路径。

**2026-10-30 P010直接输出候选：布局匹配，6帧后分配失败；用户确认随后外部重启**：独立C2分配器4K探针实测P010 3840x2160及3840x2176均25079808B、stride7680、UVoffset16711680/16711682，3840x2208为25448448B、UVoffset16957440/16957442；三尺寸10bit写入/重映射读回均通过，PROBE_EXIT0后原allocator恢复。证据out/video-decode/p010-layout-4k/live.txt。原生4K size25067520/UV7680*2176，与2160高gralloc实际对齐容量相容，但不代表任意布局可直接进口。

sq2-c2-v4l2-component.h新增默认关闭的SQ2_P010_DIRECT=1分支：fetchGraphicBlock后map等待acquire，逐项校验P010深度/采样/stride/UV偏移/plane0offset，scope结束解锁；核实两fd同inode/dev、实际容量及池预算，dup fd并用Lease持有graphic。session reuseCapture=false，DQ后交付share，再另取新lease，禁止覆盖客户端持有帧；NV12及默认copy路径保持。基线C2AllocatorGralloc源码unmap使用同步GraphicBufferMapper::unlock；实际平台/复杂fence及失败矩阵仍须继续验证，不是通用零复制保证。替换lease/close/client ownership等现有session QEMU测试通过。

显式试验owner1dbc6c64…（仅本地生成服务源码注入env，常规builder不注入）在旧b621同boot测试：自动选中并START/OUTPUT_FORMAT，C2日志OUTPUT_TIMING frames=6、copy_sync_us=0，随后SESSION_ERROR errno=-12。应用STOP_ERROR/RELEASE_AFTER_STOP_ERROR，未达到600帧或实时性。底层两次session close记录result0及capture-released；完整stage记录RUN_EXIT1、RESTORE_EXIT0和OWNER_RESTORED，旧row owner6e4209d9…已恢复。原失败码将所有fetchGraphicBlock状态折为ENOMEM，**不能据此断言物理内存耗尽**；现已补DIRECT_FETCH_FAILED真实c2_status/pool/capture_count日志，编译通过尚未实机。下一步检查C2输出池额度/等待背压及outputDelay=8与capture队列数量的关系，不能直接放大队列或重启固件重试。

SSH曾卡ECDH/关闭；后使用严格主机校验的ecdh-sha2-nistp256重新连通，但此时boot已变5e25567a-6282-42ac-a81e-d9e5c01773b3。用户明确确认“手动或其他任务重启”，不得记为自主崩溃；用户此前确认桌面触摸正常。当前guardY、无/dev/video*、实验C2服务未运行、data owner为row6e4209d9…，本轮没有在新boot装载固件。旧collector以signal结束53061ms/234records/19358B，不能称完整120秒。后续须重新核对启动入口与硬件状态，优先离线处理背压问题。

四角色补充out/video-decode/p010-direct：BASELINE/MODIFIED/ROLLBACK均实际构建并在新boot无参数本地枚举4组件exit0，rollback源字节hash一致；不注册Binder/不打开视频，不代表三次硬解。原venus-transport角色保留并扩展ledger。阶段日志/应用日志/collector已取回，完整适配目标仍未完成。

**2026-10-30 4K60 Main10真实Surface出帧，性能不通过；P010行复制有改善**：用户最新目标明确允许直接操作Surface并跳过锁屏，保留screen-only/nohibernate，不代表允许真正休眠。原93.42Mbps向量保留。首次VBV40000/40000样本因初始CPB仍有41.397495Mbps平均码率，没有部署；另存out/video-decode/main10-4k60-vbv35，VBV35000/35000、Main-tier5.1、真yuv420p10le、3840x2160/60、600帧/3B，实测36.225498Mbps。使用同签名独立org.surface.sq2.videotest.main10 APK更新user10，未更改原应用。

同boot b621a7ac/guardY、9608 owner/c859 allocator在独立watchdog窗口实测：自动选c2.sq2.hevc.experimental、hardware=true、format_supported=true，成功START/OUTPUT_FORMAT；25秒期限146输出/渲染、152输入，eos=false/realtime=false/pass=false，STOP_OK/RELEASE_OK。计时149帧copy_sync22367937us，主要问题转为输出复制性能，不是发现拒绝。门槛首次switch-user10过早返回Failed，随后确认可用时切换成功，必须以actual-user10判断，不只凭命令调用过。完整run脚本持久日志、独立恢复计时器保留。

修改sq2-c2-v4l2-component.h的deliver() P010分支，新增sq2-p010-copy.h：已验证深度/stride/capacity后，Y连续16bit及UV交错相邻2字节的布局使用现有sq2_copy_row的NEON行复制；其它有效平面布局保留逐样本fallback，不改变NV12、同步、所有权或广告。tests/sq2_p010_copy_test.cpp以参考逐样本结果比较整块内存，覆盖planar/UV/VU/列跨度、未对齐、padding及3840宽；aarch64静态/QEMU通过。新owner SHA6e4209d90a711fbb7d6ac8ecd49340156ca273e0a99d0a299b3415c1ccbe90ed构建通过。

同样APK/码流的第二窗口main10-4k60-row，collector11871/watchdog11981，普通UID1010255自动选中，真实布局rows7680/cols2,4,4/UVdelta2；25秒235输入、229输出/渲染，仍eos=false/realtime=false/pass=false，STOP_OK/RELEASE_OK、RUN_EXIT1、RESTORE_EXIT0。233帧copy_sync20875873us，其中DMA_SYNC498503us、像素复制20376543us；约87ms/帧复制仍不够60fps。晚帧截图有完整彩色渐变斜纹，非逐像素/HDR证明。不能把性能改善说成4K60通过。

两轮结束均恢复原allocator7f394d70…，row watchdog最终ORIGINAL_ALREADY_PRESENT_NO_RESTART；XML已撤销、current-user10、surfaceflinger running、boot未变、guardY，正常入口未改、未重启PAS。当前data owner保留6e4209d9…运行，backup before-row为9608。collector完整120001ms/396records/32526B取回。补充四角色out/video-decode/p010-row-copy，BASELINE/MODIFIED/ROLLBACK同无参数store本地枚举count4/exit0，恢复源hash一致；不是三次硬解。真实性能对照只有146→229帧两轮。下一步实查gralloc P010容量/UV偏移与native capture几何，设计保留所有权/栅栏的直接DMABUF输出或消除复制的路径；不可盲目取消容量检查、扩大广告或放宽realtime门槛。High-tier、其它codec/联合约束及正常启动持久化仍未完成。

**2026-10-30 Main10 4K60发现拒绝已定位到码率；纠正上一轮服务缺席判断**：重新核对kernel.log，surfaceflinger29821已在候选窗口启动，并正常访问allocator；上轮查询误用大写SurfaceFlinger造成假阴性，不能据此认定服务启动故障。init子进程崩溃在切换前已经存在。恢复原版后surfaceflinger9048的重启也与手动allocator恢复吻合。此前“框架状态异常”并非已证实根因。

本轮b窗口使用setsid+持久xtrace，collector2735/watchdog2822 READY后复用已核验9608 owner/c859 allocator，无PAS。首次稳定门槛的am get-current-user在activity暂未就绪时失败，set-e提前退出；没有重新切换allocator，另行确认system_server/surfaceflinger两PID及user10后，才继续同窗口播放器。普通应用实际得到CAPABILITY hardware=true/profiles2/format_supported=false，AUTO_SELECTED null，PLAYER_EXIT1，XML清理RESTORED，没有开始解码。随后手动恢复allocator7f…；180秒watchdog确认ORIGINAL_ALREADY_PRESENT_NO_RESTART。owner现保留9608候选running（不是默认启动部署）。

增加仅测试APK的格式/VideoCapabilities输出，原版allocator下再做发现诊断：c尝试因框架恢复后current-user0、包只安装user10而未启动，恢复user10后的d尝试实际输出INPUT_FORMAT bitrate/max-bitrate93424830、3840x2160/60/profile2/level65536；VIDEO_LIMITS bitrate=[1,40000000] size_rate=true，format_supported=false。本机FFmpeg trace_headers确认码流general_tier_flag=0、general_level_idc=153，ffprobe确认Main10/yuv420p10le/600帧、93.42483Mbps。故当前测试素材未约束码率，与Main-tier5.1声明的40Mbps不匹配；不能归为4K60硬件吞吐失败。下一步在保留原不匹配向量证据的前提下生成有VBV约束的Main-tier5.1完整4K60 Main10流，再测自动发现/Surface；High-tier/更高码率必须单独依据固件能力和实测对齐，不可直接抬高广告。普通应用Main10 4K60仍未通过。

原四角色和main10-4k60-run补充角色保留、ledger追加；该补充BASELINE/MODIFIED/ROLLBACK仍是shell语法均0及还原哈希一致，不是硬件三态对照。证据同目录capability-result.txt及b/d持久阶段日志。下一次稳定门槛必须容忍am/pidof瞬时失败、核实两个进程和user10；此为诊断脚本缺陷，不是decoder能力问题。

**2026-10-30 Main10 4K60窗口在播放前中止，已恢复**：核对安装包证书不同（原4524c6f3…、本机13879b22…），以独立包名org.surface.sq2.videotest.main10重新打包/签名并为user10安装成功，未覆盖/卸载原应用。第一次签名相对路径多一层导致exit1，修正后验证0。测试脚本副本改独立包名和hevc-main10-4k60-a，仍要求600输入/输出/渲染和realtime/EOS。原脚本未改。

同boot b621a7ac/guardY，collector29594 smoke与watchdog29685 restore-main10 main10-4k60-a READY后，备份e7da owner并临时换9608、scoped allocator c859（库22f7）。此轮新增执行了allocator restorecon；不能直接归因，但与前次成功部署不同。allocator/C2曾running，框架重启期间SurfaceFlinger缺席，临时播放器进入清理且没有result.txt，未取得任何本轮解码结果。SSH旧连接失去后续输出，但新连接一直可用；远端测试脚本已不存在，故结束本地残留SSH，不重启测试。临时XML已撤销，logcat30539已停止。不得把缺result当作4K60性能失败或固件崩溃。

已手动恢复allocator（7f394d70…hash一致），后180秒watchdog记录ORIGINAL_ALREADY_PRESENT_NO_RESTART；owner也从本轮备份恢复e7da7db7…并重启实验C2。恢复后system_server9259、surfaceflinger9048，截图确实显示桌面；boot不变、未重新PAS、未写启动盘。collector完整120000ms/415records/33110B已取回，但过滤器未保留init service/exec错误，现有日志中的Trusty/TPM循环崩溃不能归因于本次解码。证据out/video-decode/main10-4k60-run；补充四角色仅shell语法BASELINE/MODIFIED/ROLLBACK均0，回滚字节一致，非三次硬解；原四角色ledger已追加。下一步先补有界init错误采集和框架稳定门槛，查清服务窗口失败，再用新的独立label重试，不能盲目重复allocator切换。保留独立测试APK供后续使用。

**2026-10-30 本机同步后的接续检查点**：解码器分支从c19ba9b快进至65c1662，保留本机四个未跟踪音频文件，未纳入本轮。实查设备仍b621a7ac-3cc8-4d0b-8933-6faf4a7c9082、guardY/boot_completed1，allocator原版7f394d70…，C2 wrapper bdf11d60…仍running，没有重新PAS或切换服务。本机重新构建C2服务得到与检查点完全一致的9608a1f3…；本机缺失的4K60 Main10 APK已重新构建并签名验证通过，SHA256 cd406bac4017d2e32be05f5dbe0d7d0acbcbc859b99965ea77a662b0855ee4c3，600帧/Main10/yuv420p10le/3B帧。此机新生成私有测试签名，安装前须核对设备已有测试包证书，不能盲目覆盖或卸载清数据。APK尚未上传/安装/播放。

watchdog新增可选第二参数label（1..48字符，仅字母数字下划线连字符），形成同boot独立证据目录，保留无label旧路径和mkdir拒绝覆盖。tests/test_sq2_allocator_labels.py同一输入BASELINE退出1（第二个不同标签仍撞旧目录）、MODIFIED退出0（不同标签独立、重复拒绝、非法/空/过长拒绝）、ROLLBACK退出1；patch重建和还原SHA一致。原12项watchdog和5项restore保护回归通过；QEMU静态会话测试含4K P010预算/EOS/失败回收通过，非本轮硬解实测。设备仅运行新脚本observe labels-20261030-a，5秒后OBSERVE_FINISHED_NO_CHANGE、allocator哈希不变。补充证据out/video-decode/allocator-labels，原venus-transport四角色仍保留并扩展ledger。下一步核对测试包签名、加入Main10 4K60播放器独立label/600帧断言，再用新label恢复窗口部署已构建owner并做有界Surface测试；当前未证明4K60 Main10普通应用通过。

**2026-10-30 保存检查点：4K Main10独立C2六帧通过，4K60 APK构建完成未测试**：新增sq2-video-memory.h，原生P010单帧32MiB/池384MiB，NV12仍16MiB/192MiB，压缩输入和可选import/map缓存上限不变。session协商/queue及C2 dma_heap分配共用格式对应上限。P010当前不进入仅NV12的mapper缓存，不能把增大native池当作性能优化。主机ASan/UBSan测试覆盖4K P010 13槽成功/EOS、17槽超预算、单帧越界、分配第7槽失败后全部回收及NV12旧限制，原生命周期/错误/重用测试也通过。

HEVC向量脚本新增--size 320x240/1920x1080/3840x2160，P010样本检查改array避免巨量Python整数；默认小向量行为保持。组件harness按向量尺寸复制/算CRC/配置接口，4K等待60秒；builder新增--vector-dir/--output-dir保留旧产物。生成hevc-main10-4k-vector为3840x2160、真10bit、6帧无B帧，独立FFmpeg P010参考。新C2服务本地构建SHA9608a1f372af6f86676819c560e03c2cc3649b011e2eb921afB6AB1E6C8A5E5E（大小写无关），**尚未替换运行owner e7da7db7…**；默认AVC harness新源码编译通过，尚未新实机回归。

同boot b621a7ac…驻留native，仅运行独立/data/local/tmp/sq2-c2-main10-4k-test --frames，没有重新启动PAS或切换allocator。4K六帧Y/UV CRC全匹配，C2_RESULT done7/mask3f/eos1/errors0，C2_STOP0/C2_RELEASE0/CLIENT_BUFFERS_RELEASED、TEST_EXIT0。结果保存在out/video-decode/hevc-main10-4k-vector/sq2-c2-main10-4k-result.txt。collector20204有smoke并完整结束120000ms/234records/15846B，已取回同目录。此为root独立C2+gralloc大帧正确性，不是普通Surface4K10bit或60fps性能证明。

4K60 Main10普通APK已构建完成（exec78479正常exit0，无待轮询任务），目录out/video-decode/player-app-3840x2160-hevc-main10-60fps；600帧/真yuv420p10le/Main10/3B帧。尚未上传安装，当前设备测试APK仍Main8 4K60。下一步先备份并部署新owner、为本boot新的allocator测试窗口加唯一label和固定恢复保护，再测4K60 Main10普通应用；已有restore-main10-b621…目录已使用，不得覆盖。当前allocator原版7f394d70…、C2 wrapper bdf11d60…/owner e7da…、native驻留，正常入口未改、临时XML已撤销。其余格式/联合约束/持久化仍待完成。

**2026-10-30 Main10普通应用通过，Main8 4K60回归通过**：重新核对BC711/1000215216、ESP B027-0512/marker、data UUID4f46d2ec…、loop0 p3/root来源、菜单d1ae9af6…和p010-stride镜像e519556b…后，仅原子替换一次性env（d762d0a0…），旧env备份before-main10-scoped，主动Android reboot。新boot **b621a7ac-3cc8-4d0b-8933-6faf4a7c9082**，guardY/boot_completed1，core a5f2bcc5…/dec66e9c26a…匹配。首次用户切换期间SSH短暂断开，同boot恢复；collector6338 smoke确认后本boot仅一次lab-start成功。不得重跑PAS。

data owner e7da7db7…、private-probe bdf11d60…、C2 mapper eb31d402…哈希均核对；将RAM /vendor/bin/sq2-c2-experimental替换为private-probe，chmod755/chcon hal_codec2_default_exec，rc仍root/kernel域，重启服务running。正式allocator原exe7f394d70…另存backup，候选lib22f7e4c2…及scoped launcher c859a014…恢复到RAM；新增watchdog restore-main10模式180秒、新hash固定、独立目录，三模式12项主机测试通过。watchdog14886 READY后才切换allocator，默认服务found，C2仍running；新应用collector18663另行启动且smoke落盘。

普通UID1010254运行hevc-main10-small，自动选c2.sq2.hevc.experimental，hardware=true/profiles2/format_supported=true，profile2/Main10、320x240/30/300帧。结果 **300输入/输出/渲染、9962ms、EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED**。晚帧截图人工查看有彩色渐变斜纹测试图案；不是HDR或逐像素证明。完整目录out/video-decode/hevc-main10-vector/sq2-player-hevc-main10-small-b621a7ac-3cc8-4d0b-8933-6faf4a7c9082已取回。这是首次普通应用Surface Main10链路通过，不能扩展为4K10bit能力。

同一候选仍运行期间安装已有HEVC Main8 3840x2160/60 APK，hevc-4k60普通应用自动选同组件，**600输入/输出/渲染、9978ms、EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED**；结果/截图目录同上hevc-4k60已取回。当前测试APK为Main8 4K60。AVC在本轮组合下尚未新回归。

boot collector完整KMSG_END120000ms/366records/27201B，app collector完整KMSG_END120000ms/287records/20647B，覆盖两次播放结束，均已取回hevc-main10-vector/sq2-main10-scoped-*；app未见venus/HFI/BUG/Oops/panic/OVERRUN记录，但有audit限流，不能称全量无错误。临时XML已撤销。180秒watchdog最终ORIGINAL_RESTORED/RESTART_REQUESTED，原allocator7f394d70…running/default found，boot不变；用户10唤醒/跳过锁屏、timeout2147483646恢复。C2实验wrapper和native仍驻留，allocator已回原版，因此再次Main10 Surface测试前需重新布置已验证候选和新的独立回退窗口，不可直接重复本boot同名证据目录。

剩余范围：Main10 4K/60端到端内存预算（native4K P01025067520B高于当前16MiB限制）、VP9 profile2、MPEG适配、联合尺寸/吞吐约束、AVC回归及正常启动持久化仍未完成。不得把本轮小尺寸Main10+8bit4K60拼成4K60 Main10通过。

**2026-10-30 收窄LD路径后真实allocator服务注册通过，定时恢复通过**：watchdog新增restore-scoped模式，仅识别新c859a014…，独立run目录保留旧证据。两模式恢复/原版/no-unknown/坏备份8项主机检查通过；Android手动restore也增加新hash白名单。新sample-sq2-allocator-start.sh从initramfs做三次有界只读PID/exe/maps/service check取证。

重核boot6e42d2b5…/guardY/boot_completed1/原版exe及backup7f394d70…/candidate22f7e4c2…后上传新launcher并校验c859a014…；logger17008、watchdog17009、sampler17010先启动，READY及活跃确认后原子换入launcher并restart。快照第一项为旧PID32438，第二/三项为候选PID17456，exe为sq2-allocator-original，maps明确候选专用目录libminigbm与/system/lib64/libbinder{,_ndk}，两次IAllocator/default均found；SSH实际可连、服务running/boot_completed1。由此验证本轮收窄路径解决了默认服务注册问题，不是Main10呈现或长期运行证明。

45秒后定时器仍按计划恢复原版，日志ORIGINAL_RESTORED/RESTART_REQUESTED；boot未变，原exe完整SHA7f394d70…，默认服务found/running。用户10唤醒/跳过锁屏、timeout2147483646再次恢复。logger17008已无/proc项，快照和logcat已取回out/video-decode/minigbm-p010/allocator-scoped-*。两轮logcat实际均达到4194304字节限制而截断，**并非完整90秒窗口**；旧retry覆盖到回退后，新scoped覆盖候选启动及约14秒后。后续应区分窗口timeout与文件限额，并保留独立快照/回退证据；threadpool0警告在成功候选中仍出现，不能单凭该警告判断卡死。

当前运行原版分配器，candidate库及backup在RAM vendor保留；新launcher在data。正常入口无驻留native，下一步进入已验证的p010-stride实验入口（必须重新核对ESP身份/一次性env），恢复新C2 owner及scoped allocator，进行Main10普通APK，并回归Main8/AVC。不把注册通过当作Main10普通应用成功，也不取消P010/4K内存上限问题。正常启动持久化仍未完成。

**2026-10-30 有界真实服务诊断自动恢复；发现LD路径改变Binder依赖**：新增sq2-allocator-watchdog.sh（initramfs根、root cgroup、setsid）及实际脚本隔离恢复测试test_sq2_allocator_watchdog.py。root/Android共用mnt namespace但根inode不同，已修正前置检查；observe空跑PID26321完成，无框架重启。主机restore/unchanged/unknown/bad-backup四项通过。正式试验先备份原exe7f394d70…到vendor/bin/hw/sq2-allocator-original，并准备candidate22f7e4c2…；独立logcat进程30059（timeout90、ulimit-f8192）及watchdog30060（45秒）确认活跃/READY后，才原子切换旧launcher249da7ca…并ctl.restart。

再次断网，但45秒后watchdog在Android框架之外恢复原exe并请求restart，SSH恢复；日志明确ORIGINAL_RESTORED/RESTART_REQUESTED，boot仍6e42d2b5…，新原版allocator PID32438/running/hash7f394d70…，boot_completed1。用户10已切回、唤醒/跳过锁屏、timeout2147483646恢复。没有主动重启整板。watchdog已结束，logger30059的/proc条目也已不存在；90秒日志完整取回out/video-decode/minigbm-p010/allocator-retry-logcat.txt。原始日志不得提交。

日志候选PID30493已经打印Minigbm AIDL allocator starting up，随后libbinder.BackendUnifiedServiceManager在IAllocator/default路径报threadpool0；SF持续等待默认服务直到回退。未见候选明确链接失败，不能再归类为未能执行main。关键对照：正常allocator maps的Binder来自/system/lib64，旧launcher把/vendor/lib64:/system/lib64加入LD_LIBRARY_PATH后，--list选了/vendor/lib64/libbinder.so并改变namespace共享依赖；可能导致注册路径异常，但尚未证明唯一因果。只设候选目录的--list恢复system Binder，同时仍选candidate+vendor donor。

已将sq2-allocator-launch.c收窄LD_LIBRARY_PATH为/vendor/lib64/sq2-p010-allocator；新编译allocator-launch-scoped SHA c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1，尚未上传或真实服务验证。旧launcher产物保留用于取证，禁止再次作为新候选使用。下一轮先给恢复脚本/定时器添加新hash的明确白名单和相应测试，采用新的唯一run目录（本boot restore目录已经存在，当前脚本会拒绝重跑），验证新候选后才有界切换。不能绕过重跑保护或让timer误拒新hash。Main10应用仍未通过。

**2026-10-30 原launcher隔离执行链通过**：新增 sq2-allocator-isolated-probe.c，root创建独立mount namespace并递归MS_PRIVATE，将data中的isolated-hw绑定到该进程的/vendor/bin/hw，将候选目录绑定到/vendor/lib64/sq2-p010-allocator。随后降为uid1000/gid1003/supp1026，设置有效/许可/继承及ambient SYS_NICE，执行原SHA249da7ca…launcher；原launcher设置LD_LIBRARY_PATH并清LD_PRELOAD，执行对应backup路径的诊断ELF。15秒alarm继承至exec，不注册默认服务。未复制SELinux域/完整cap bounding/task profile/init管理语义，不应称为init启动等价。

prepare脚本新增--needed-stub，仅对私有诊断ELF把无版本依赖的libm.so改为libs.so，stub自身DT_NEEDED保留libm；注册导入仍是不可转发的SQ2专用符号。有效诊断ELF SHA3925d62d98df3776b1717f7fc602a1f8261e3dd80a7d6f2cd64010ce25a5a75d。第一版改liblog的方式破坏verneed而被linker拒绝，8f13b3…产物废弃，不是实际allocator故障证据。正式helper/有效副本执行输出ISOLATED_LAUNCH、REGISTRATION_INTERCEPTED valid1/flags1/uid1000、exit0；allocator/SF/system_server仍2016/2049/2327，原服务exe仍7f394d70…。全局vendor只新增一个空的sq2-p010-allocator挂载点目录，库/可执行文件替换全在子进程私有namespace，退出即消失；data staging保留。

该结果没有复现原服务失败。后续若做真实注册/服务切换，必须先补initramfs侧独立定时恢复及完整stderr/logcat采集，回退不得依赖Android框架或Wi-Fi；不要把本次诊断通过作为直接重试实时切换的依据。Main10普通应用仍未通过，正常启动能力不变。

**2026-10-30 实际 allocator 注册前诊断通过，未触碰正式服务**：先取回并验证原exe7f394d70…，readelf确认实际版本唯一注册导入为 AServiceManager_addServiceWithFlags@LIBBINDER_NDK34（与参考源码旧API有差异）。新增 prepare-sq2-allocator-registration-probe.py，固定原exe哈希，只在私有副本将该导入等长填充改名为 SQ2_allocator_probe_registration；拒绝未知输入/覆盖输出。产物SHA ecb8480ca33c834d16a69d4e98bf347ff5ff210b0cf6ad4f137f2a7a61cd4f30，readelf核验原注册导入已不存在。

probe/video/sq2-allocator-registration-probe.c提供专用符号，精确检查非空binder和默认allocator实例名，打印后_exit，不调用/转发真实注册。DSO以LIBBINDER_NDK34版本导出，仅用于诊断副本。实机没有LD_PRELOAD时副本明确CANNOT LINK/exit1（无法意外落回真实注册）；加载stub后，原版和候选22f7e4c2…两套LD_LIBRARY_PATH均REGISTRATION_INTERCEPTED valid1/flags1/uid0/exit0。每项timeout15秒，原allocator/SF/system_server PID2016/2049/2327前后不变。未替换正常HAL、未重启框架、没有新native/PAS操作。

这验证实际程序在root直接启动时已完成注册前初始化，不等同init服务的UID/能力/SELinux/命名空间、原launcher exec链或真实注册成功。候选不能因此发布。下一步复核原launcher路径与init环境；仍须保留不能注册的诊断副本，不得把正常原exe当作该副本执行。Main10 Surface问题尚未解决。

**2026-10-30 分配器注册前 driver 初始化独立对照通过**：当前 boot6e42d2b5…，正常 allocator/SF/system_server 为2016/2049/2327。新增 probe/video/sq2-gralloc-init-probe.cpp，以 RTLD_NOW dlopen 指定库并通过准确导出 _ZN19cros_gralloc_driver12get_instanceEv 调用 donor 的 std::shared_ptr<cros_gralloc_driver> 返回接口；只使用不完整类型/shared_ptr 控制块，不读私有对象布局、不注册 Binder、不运行 allocator main。15秒 alarm，释放客户端引用，保留 DSO 到进程退出以匹配 donor 的静态 singleton 生命周期。

原版与 candidate22f7e4c2…分别在 root 下 GRALLOC_INIT_OK=1/exit0；随后 --system 在 dlopen 前降为 uid1000/gid1003/supplementary1026（实读正式进程），两者同样成功。未保留 SYS_NICE、未复制 init SELinux 域/命名空间/exec 链，因此不是完整服务启动等价测试。正式三进程 PID 前后不变，未替换图形服务。依赖能枚举、get_instance 能初始化，仍不能证明启动 launcher/实际服务注册或 Surface 呈现成功；下一步需取实际 exec/注册前诊断，避免重复实时替换。探针产物和设备路径均在 minigbm-p010/sq2-p010-gralloc 下。

**2026-10-30 黑屏反馈后已恢复正常启动，取回切换日志**：用户确认此前切换后卡住或黑屏。随后只读连接成功，boot ID 为 6e42d2b5-99ab-4d9b-9769-92e6d3dc5d03，boot_completed=1，screen_only=Y，cmdline 保留 nohibernate、正常 closed Vulkan 入口；本轮未发送 reboot。allocator 服务 running，原 exe 完整 SHA 7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da，全局 mapper c1a5708286a42723d1ea8640d08e2486948c382a1531a08173cee1f77a25c283。RAM 候选已回退，无需运行恢复脚本。开始当前用户为0，已将用户0和10的 screen_off_timeout 设为2147483646，并 switch-user 10、WAKEUP、dismiss-keyguard；实读当前用户10。未修改凭据或休眠保护。

旧持久 collector 已取回 out/video-decode/minigbm-p010/allocator-switch-kernel.log，104515字节，KMSG_END timeout/120000ms/597records/104434payload bytes。日志证实 allocator onrestart 执行了 restart surfaceflinger，原 SF1998 被杀，新 SF29784 启动，zygote也重启；随后持续约两分钟无法找到 IAllocator/default，Wi-Fi 记录本机主动 DEAUTH_LEAVING。因此先前命令至少到达 allocator restart，不能继续写成全部步骤均未知；候选实际加载库/失败原因仍缺证。持续的内核collector也表明这不是已证实的瞬间内核硬挂。audit 丢失/限流存在，不能以没有错误记录排除故障。

不重试实时服务替换。只执行 linker64 --list（不运行服务 main）：原 allocator 配合 data 目录候选 LD_LIBRARY_PATH 可枚举到 candidate、vendor donor 及完整依赖，launcher 自身依赖也可枚举；这不是 init UID/能力/命名空间下的实际启动验证，也不能证明符号重定位或注册通过。当前32个 tombstone 的 Cmdline 均为已有 init/trusty/gscd，没有保留 allocator tombstone；不能据此排除先前 allocator 崩溃。下一步需设计不触碰正在运行默认服务的启动/注册前诊断并保留 stderr，定位真实服务启动失败，再安排受控呈现测试。Main10独立分配与原生六帧结论保留，普通应用及4K10bit仍未通过。

**2026-10-30 分配器恢复工具就绪，设备仍不可达**：后续多次 SSH 到 192.168.8.194 超时，工作站邻居表该地址为 Incomplete，无法确认当前 boot、服务状态或候选是否落地；不据此推断整机崩溃，也不扫描其它设备冒认目标。上一切换 session98048 已终止，不存在待轮询的运行任务。

新增 `probe/video/restore-sq2-allocator.sh`，仅在 root、screen_only=Y、目标精确匹配已知 launcher 249da7ca…且备份匹配原版 7f394d70…时，复制到临时文件、复核完整 SHA、sync、原子恢复目标，再重启 vendor.graphics.allocator。目标已经是原版时不重启；未知目标或损坏备份拒绝。全局 mapper 不动。`tests/test_sq2_allocator_restore.py` 在隔离目录执行实际脚本，restore/already/unknown/bad-backup 四项通过。这是主机逻辑验证，脚本尚未上传或实机执行。

恢复连接后先读取 boot ID、服务状态和目标/备份哈希及进程 maps，再决定是否执行回退；若已经正常重启，RAM 候选可能已消失，不应重复切换。还需核对当前用户的 screen_off_timeout（上次已验证为 2147483646），保留手动关屏及内核 guard。Main10 普通应用仍未通过，不能把离线回退验证当成硬解完成。

**2026-10-30 allocator服务候选切换命令未返回，SSH暂不可达**：实际init rc服务vendor.graphics.allocator，user system/group graphics drmrpc，seclabel hal_graphics_allocator_default，只有SYS_NICE，onrestart restart surfaceflinger。因此未用需要SYS_ADMIN的私有mount方案，改为专用原生launcher只对exec的allocator设置LD_LIBRARY_PATH=/vendor/lib64/sq2-p010-allocator:/vendor/lib64:/system/lib64，unset LD_PRELOAD/SQ2_VIDEO_MAP_CACHE；全局库路径不替换。原exe SHA7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da，全局lib c1a57082…，exe/lib标签均tmpfs。操作前SF1998/system_server2140，boot38e9e63c…。

launcher源码probe/video/sq2-allocator-launch.c、产物minigbm-p010/allocator-launch SHA249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e，上传data/sq2-p010-gralloc。独立collector sq2-p010-allocator-switch-kernel.log smoke和candidate22f7e4c2…校验输出已收到。随后发出的set-e链计划：备份原exe到/vendor/bin/hw/sq2-allocator-original及/data/local/tmp/sq2-p010-gralloc/allocator-original；在RAM vendor/lib64/sq2-p010-allocator放candidate；原子替换服务exe为launcher；ctl.restart vendor.graphics.allocator；sleep4后输出服务/PIDs。**exec session98048随后以Connection reset/exit1结束，仅收到smoke/hash，后续步骤完成情况未知。** 新SSH连接超时，尚无服务/PID/画面确认。不要重发切换命令、不要擅自重启；98048已终止，先读设备状态。已询问用户当前屏幕状态，待答。

恢复后先核对实际exe/backup/服务pid的maps，确认是否只allocator加载新库（SF应仍全局c1a57082…），以及bootID判断框架重启还是板重启。若服务失败，核验原exe备份hash后原子恢复服务路径并ctl.restart vendor.graphics.allocator；全局库本来未变，无需替换全局mapper。重启会清RAM vendor候选，但本轮未主动发reboot。不得把命令发出当成部署成功或把网络断开解释成Main10硬件不支持。

**2026-10-30 P010显示usage独立分配候选通过，未替换全局服务**：新增只读sq2-drm-plane-formats.c，O_RDONLY打开card0、仅fd级UNIVERSAL_PLANES/GETPLANERESOURCES/GETPLANE，无master/framebuffer/modeset。实际四个视频plane33/39/45/51均公布P010，另外四个RGB plane没有；固定内核dpu_plane_format_mod_supported接受linear。不能把格式枚举当成实际scanout呈现通过。

minigbm固定源msm.c SHA991257af…只在SC_7280给P010添加SCANOUT；新增sq2-minigbm-p010.h以DRM_MSM_GET_PARAM读真实CHIP_ID，仅0x06080001允许增加LINEAR_METADATA/P010的BO_USE_SCANOUT，不定义SC7280、不改其它格式/UBWC/GPU身份。prepare-sq2-minigbm-map-cache.py --p010-scanout生成独立/root/.cache/sq2-video-minigbm-p010，保留原映射缓存补丁；builder新增SQ2_MINIGBM_STAGE/OUT变量，原默认路径保持。候选out/video-decode/minigbm-p010/libminigbm_gralloc.so SHA22f7e4c2bd76338519964d3e67d2392e4e7e35aeb5904fb2b6c0fa393603ce2c。

新增sq2-minigbm-p010-probe.c独立dlopen指定库，renderD128上请求SW_READ/WRITE_OFTEN+TEXTURE+SCANOUT P010320x240，仅分配/释放无渲染命令。boot38e9e63c…旧全局库得到SUPPORTED0/ALLOCATED0/exit3，新候选SUPPORTED1/ALLOCATED1/RELEASED/exit0。原始结果与DRM格式清单取回out/video-decode/minigbm-p010。设备候选及allocate-probe在/data/local/tmp/sq2-p010-gralloc；未bind、未替换/重启allocator或SurfaceFlinger，默认服务/全局mapper仍原样。

下一步检查allocator init rc/实际库与donor依赖，准备仅allocator进程私有mount namespace覆盖的候选启动及回退，保持系统其它图形进程库不变，然后Main10普通APK重新测试。已有C2私有mapper仍eb31d402…，owner e7da…，XML已撤销；切勿把独立分配成功称为Surface或Main10播放通过。

**2026-10-30 Main10普通应用失败定位到Surface分配usage**：boot38e9e63c…。重核旧启动服务4e7d98a7…、data owner67b326ca…、private-probe bdf11d60…/mapper eb31d402…后，备份data/sq2-c2-owner-before-main10（67b3）及sq2-c2-service-before-main10（4e7d），停止服务、以e7da7db7d9442a21105cb3c8044dae128365ba35ee77d41a5ec1e21068a535b9替换data owner，并将private-probe复制到RAM vendor服务入口；恢复标签/start，running。全局mapper未变。新服务只RAM，正常镜像未改。

安装Main10小APK，runner新增hevc-main10-small，独立collector smoke后临时XML运行。普通UID1010254自动选c2.sq2.hevc.experimental、profiles2/format_supported=true、Main10 profile2/CSD2387；原生捕获11buffer成功，但无输出即MediaCodec NO_MEMORY，STOP_ERROR/RELEASE_AFTER_STOP_ERROR；runner完成RESTORED、XML撤销，服务仍running。完整目录out/video-decode/hevc-main10-vector/sq2-player-hevc-main10-small-38e9e63c-19ab-4f53-95fb-25413f559844。

完整logcat明确：全局android.hardware.graphics.allocator-service.minigbm拒绝YCBCR_P010/DRM_P010 usage0x933（CPU read/write + GPU_TEXTURE + COMPOSER_OVERLAY）；GraphicBufferAllocator alloc320x240/format54失败，C2IgbaBuffer allocation transaction failed。不是native解码失败，也不是默认CPU-only allocator不支持P010。私有mapper仅影响服务import，不改变全局allocator进程。源码msm.c P010在texture_source_formats中，BO_USE_SCANOUT只在SC_7280编译条件给P010增加；当前SQ2不能盲开SC7280或冒充其它GPU。下一步核对实际DRM平面/P010与客户端合成分配策略，或以受控C2非Surface池交付验证真实GPU导入；保持既有图形fix，不因usage拒绝判整个Main10不支持。大段init子进程crash日志是已有trusty服务噪声，未据此断言板重启。

全量logcat和collector已取回out（含原始日志不提交）；当前collector完整收尾需核对。当前APK为Main10、owner为e7da、wrapper bdf11d60，XML已撤销。普通应用未通过，界面/flush/4K10bit尚待继续；不发布永久能力。

**2026-10-30 Main10配置自动选择P010并通过六帧**：Sq2DecodeInterface新增HEVC Main10 profile与由profile计算的output pixel format（Main10=0x36，Main=0x23；其它codec常量0x23）。组件移除显式构造format覆盖，每次打开会话查询picture-size+pixel-format，据此选择native P010/NV12；因此flush后重新打开也重新查询，但尚未实测flush。独立harness通过config_vb配置Main10并确认P010，不再靠构造参数绕过接口。

设备参数测试Main10->Main切换pixel随之变化，VPx拒绝不支持profile/AVC与尺寸/pool检查通过，参数数11。随后普通组件Main10协商测试MAIN10_PROFILE_SELECTS_P010、done7/mask3f/eos1/errors0、六CRC匹配、停止释放0/客户端buffers释放、exit0。两份日志已取回hevc-main10-vector/sq2-c2-main10-{parameters,negotiation}-result.txt；仍独立root harness，不是普通APK/Surface。

新store本地产物SHAe7da7db7d9442a21105cb3c8044dae128365ba35ee77d41a5ec1e21068a535b9（未上传/替换服务）。build-sq2-video-app.sh新增hevc-main10：真10bit geq源、libx265 Main10/3B帧，生成player-app-hevc-main10/sq2-video-test.apk；vector.json核验Main 10/yuv420p10le/300帧（320x240/30），尚未安装。下一步先备份/恢复新boot的私有wrapper+owner并临时XML测试普通APK，再Main8/AVC回归；不可假定此前RAM wrapper仍存在。Main10小尺寸路径已验证，但当前16MiB预算不支持4K10bit，未发布正常启动能力。接口候选现在含Main10，上线前仍须处理尺寸联合约束/预算，不能把本地编译当成全尺寸支持。

**2026-10-30 Android C2 Main10六帧像素通过，AVC回归通过**：Sq2C2V4L2Component新增末尾显式outputFormat参数（缺省NV12，注册store旧调用不变），传给native session。P010输出使用HAL0x36，验证实际geometry/fourcc、2字节行宽/有效payload、gralloc10bit/16bit/shift6/非大端/采样和步长；按源stride和目标rowInc/colInc逐sample复制16bit，不做8bit缩减。旧8bit优化复制路径保留；16MiB单帧/192MiB预算未扩大，4K10bit仍未支持。

build-sq2-c2-component-test.py新增--codec hevc-main10，使用已验证原生Main10向量并显式P010输出；harness按C2平面语义重组P010，与独立参考六帧Y/UV CRC比较。boot38e9e63c…运行/data/local/tmp/sq2-c2-main10-test --frames：done7/mask3f/eos1/errors0，六帧匹配，queue/stop/release0、CLIENT_BUFFERS_RELEASED、exit0。按相同新组件源码重编AVC默认NV12回归也done7/mask3f/eos1/errors0、停止/释放0、exit0。完整结果已取回out/video-decode/hevc-main10-vector/sq2-c2-main10-frames-result.txt和sq2-c2-p010-avc-result.txt。

**这是独立root C2组件+真实硬件+真实gralloc验证，不是普通UID MediaCodec/Surface/Main10能力声明。** 当前接口仍只广告HEVC Main，测试通过构造参数明确选择P010，不假称完成profile协商；运行store未替换。下一步将Main10参数与输出pixel format/自动选择贯通，验证配置/flush/重建，再普通APK；VP9 profile2、4K10bit预算、MPEG及持久化仍开放。没有重启/重初始化native或更改正常入口。

**2026-10-30 Android P010 allocator实机通过**：sq2-c2-allocator-test.cpp新增显式--p010（默认0x23旧分支保持），通过平台Codec2 allocator分配HAL_PIXEL_FORMAT_YCBCR_P010=0x36，验证3平面/10bit/allocated16/rightShift6/非大端/行列步长/采样边界，然后写带10bit低位的不同YUV图案、unmap/remap并逐sample读回。build-sq2-c2-component-test.py新增--allocator-test独立输出allocator-check/interface-test，固定头与库校验保持，不覆盖组件产物。

设备boot38e9e63c… Android全局原mapper（非私有缓存wrapper）运行exit0，320x240/256/288均P010_10BIT_REMAP_READBACK_OK/RELEASE_DONE。实际布局：Y row768/col2，U/V row768/col4；10/16/shift6；240和256高UVoffset196608/196610、分配307200B；288高UVoffset221184/221186、分配344064B。因此C2图像stride768与native P010 stride640不同，后续必须逐行/按plane布局复制，不能整块memcpy。只验证分配映射与读回，不是C2解码/Surface显示/Main10广告。

探针/data/local/tmp/sq2-c2-p010-allocator，结果sq2-p010-allocator-result.txt已取回out/video-decode/hevc-main10-vector。原生后端未新增解码命令、未重启PAS。下一步给Sq2C2V4L2Component加明确P010输出选择和16bit样本复制，先独立Main10 C2六帧参考harness再启普通应用；注册store/接口目前仍8bit，不提前广告。当前未提交变更继续保留。

**2026-10-30 Main10原生六帧通过，NV12回归通过**：新boot38e9e63c-19ab-4f53-95fb-25413f559844/boot_completed1，core a5f2bcc5…/dec66e9c26a…匹配；独立sq2-p010-stride-boot-kernel.log smoke后本boot仅一次lab-start成功。user10/唤醒/跳过锁屏/timeout2147483646已恢复，guard由lab-start校验Y。

原decode-test-crop --dmabuf（不dump、不改严格检查）Main10六帧Y/UV CRC全匹配mask3f，6输入归还、drain1/LAST1/ret0，双STREAMOFF0/CLOSE_DONE，TEST_EXIT0。捕获P010 320x256、stride640、crop320x240，有效payload245760；原较大sizeimage294912保留。随后新编译decode-test-nv12对原8bit HEVC六帧同样mask3f、全部输入/EOS/停止/关闭通过，exit0。两套结果/native-start和当时collector已取回out/video-decode/hevc-main10-vector/sq2-p010-stride-*.txt/log；collector完整收尾还需取回。**只证明原生320x240 Main10完整解码与8bit回归，不证明AndroidMain10、4K10bit、长片或完整能力对齐。** 当前Android服务仍镜像旧版，67b3owner没有重启后重新部署；下一步验证C2 P010分配布局，再接入10bit图像复制/参数与普通应用。现有allocator探针probe/video/sq2-c2-allocator-test.cpp只测0x23，需要独立新增P010分支；本地minigbm msm.c已有P010支持，实际allocator仍待测。

**2026-10-30 P010行距修正一次性重启已发出**：新增pack revision p010-stride固定a5f2bcc5…/66e9c26a…模块；镜像out/video-decode/boot-candidate/capabilities-p010-stride.cpio.gz SHAe519556b0d5e5aa7ccb98772ef58fceee6fad2647f905cf85a914cab58a60b7b。打包审计仅core/dec/manifest改变且密码addon最后。boot31cf07bf…期间重核BC711/ESP UUID/marker/data UUID/root backing、once0、ESP余517660KiB后上传；设备SHA及gzip -t通过，sync后原子改名native-c2-capabilities-p010-stride.cpio.gz。菜单备份grub.cfg.before-p010-stride（旧7c03e5a6…），新菜单d1ae9af6f9dc4c7e54d8fd37fda108e36f130393394f303b72661f95ad156e06，仅实验initrd路径变化。备份once0并核验d762d0a0…后原子设once1，sync并主动Android reboot，session36909，等待新boot确认，不重复重启。

上一轮两个collector完整取回hevc-main10-vector，layout日志KMSG_END timeout120013ms/290records/20130bytes。新编译8bit HEVC runner decode-test-nv12已上传/data/local/tmp，Main10用已在设备的decode-test-crop（不dump），两者严格CRC。下一步新boot核验哈希/guard/boot_completed，独立collector后只一次lab-start，然后Main10六帧完整及NV12回归。Android10bit尚未接入；本地minigbm msm.c已经列出P010 texture-source/分配支持，仍需实机allocator/C2布局检查。

**2026-10-30 P010首帧逐字节参考匹配，行距修正候选已编译**：boot仍31cf07bf…，已有日志无venus会话错误、上轮双STREAMOFF/CLOSE返回，未重启PAS。在独立新collector smoke后用新增--dump-first NEW_FILE /dev/video0做一次受限诊断：最多1MiB、O_EXCL/O_NOFOLLOW、DMA_READ同步、fsync，仅保存首个有效payload，不改变原geometry/CRC判定。实际保存245760字节，仍short-frame/exit1、双STREAMOFF0/CLOSE_DONE。原始payload及result已取回hevc-main10-vector；按stride640/Yscan256提取可见Y/UV，与独立FFmpeg ref.p010第0帧**全部230400字节相同**，CRC582aa82b/350913a6；stride640/scan240不匹配，768布局无法放入有效payload。结果含原始/参考SHA在p010-first-layout.json；只是首帧，不是六帧/EOS或AndroidMain10通过。

本地固定kernel的drivers/gpu/drm/msm/disp/dpu1/msm_media_info.h SHA88ee1ae38078035cc96ee064e370ae4389333f6c0ce3135a83d9d0c87a543a9a，VENUS_Y_STRIDE/UV_STRIDE线性P010为ALIGN(width*2,128)，P010_UBWC才256；Yscan32/UVscan16。prepare脚本已将暴露仅SQ2的P010 bytesperline修正为128对齐，分配sizeimage仍保守沿用原256对齐预算，不改其他格式。新stage/root/.cache/sq2-video-p010-stride/venus编译完成session59170 exit0，core a5f2bcc54b0ec523c8df886f1675f4dbb9c934c9756e373028bbe307f4d1af0f、dec66e9c26a9602a7d01a4f0f4d74110967f7aa90af85d449a24bfa16a2c1315a47，符号missing0。尚未添加pack revision/打包/部署。下一步新独立镜像启动后完整六帧Main10 CRC+EOS，再做NV12回归；AndroidP010路径仍待实现。runner原有主机阶段/回收测试通过，新dump路径实机只验证本次小payload，不代表所有故障分支已测。

**2026-10-30 裁剪修复实机生效，Main10首帧布局仍失败**：新boot31cf07bf-f0b2-4a66-8163-96550013d9c5/boot_completed1，core bb835d4d…/dec f57dbf81…匹配。独立collector sq2-p010-crop-boot-kernel.log smoke后本boot仅一次lab-start成功。user10/唤醒/跳过锁屏/timeout2147483646已恢复。首次decode-test-crop --dmabuf输出CROP_BEFORE_FORMAT=320x240，S_FMT(P010)后crop仍320x240，裁剪修复通过实机；capture宽320高256、stride768、size294912。捕获10buffer/STREAMON成功，返回input0及首个capture bytes245760/offset0/flags4008，但严格need校验报short frame，未作CRC，exit1；两个STREAMOFF0、CLOSE_DONE。

不能称Main10解码通过。245760与报告stride768/scan256的布局不一致；可能P010实际stride/scanlines或固件bytesused定义，需有界采集真实buffer并与独立P010参考比较，不能直接忽略short-frame或猜stride上线。完整result/native-start/当时collector取回out/video-decode/hevc-main10-vector；collector收尾待取。当前native仍驻留，没再试、没热卸，Android服务旧版。下一步先看已有日志/代码，必要时独立新启动做一次有界帧布局取证。

**2026-10-30 裁剪保留候选已编译/打包，正在上传**：新增venus-sq2-crop.h，在SQ2且reconfig（SOURCE_CHANGE已到）、捕获队列、协商存储宽高未变、原裁剪非空/非负且边界有效时，vdec_s_fmt保留固件crop；其它情况仍重置。tests/sq2_crop_test.c在ASan/UBSan覆盖实测320x240-in-320x256、非零offset、尺寸改变、非SQ2/非捕获/无reconfig、负值/空/越界/UINT32_MAX。runner新增G_SELECTION前置日志使旧mock缺ioctl，已补只读selection fixture，资源/阶段测试通过。

新stage/root/.cache/sq2-video-p010-crop/venus编译通过；core bb835d4da5faa9282c98dbb02083fbf4738dbef4d2a2ff1b83f5538b681ef6ce，dec f57dbf811968e4ac03f21b3b917c093ee0f2155276cc0d91038fc214a7dec79d，符号审计missing0，格式helper、640停止序列、10END案例通过。打包--revision p010-crop输出capabilities-p010-crop.cpio.gz SHA500f153b46ae0e62ce9b3764c9e710040afa9a45db79312265dea2dd308e8410，仅core/dec/manifest变化、密码addon最后。当前boot仍85c96f2e…；身份/ESP/UUID/marker/root backing重核通过，ESP余711396KiB。旧collector完整取回KMSG_END timeout120001ms/334records/25321bytes。新镜像SCP session40568已完成；设备端SHA/gzip校验通过，sync后原子改名。备份grub.cfg.before-p010-crop及sq2-c2-test.env.before-p010-crop，核验新菜单/once文件后原子替换、sync并主动Android reboot（session48944）。待新boot确认，不重复发reboot。准备菜单out/video-decode/boot-candidate/grub.p010-crop.cfg SHA7c03e5a64261cc95ffc4915b9f5d11cdfce2d84f75f5406549b78f2ec1c0c117，仅实验initrd路径改变。

**2026-10-30 P010新启动成功；Main10发现S_FMT裁剪覆盖**：新boot85c96f2e-d1ab-412d-ad9f-2c68c018b7d3、boot_completed1、guardY，core eb2a8725…/dec ce3366cd…匹配。独立sq2-p010-boot-kernel.log smoke通过后，本boot仅一次video-decoder-lab-start成功；禁止重跑/热卸。已切user10、WAKEUP/dismiss-keyguard并设timeout2147483646。当前Android服务为启动镜像旧版，未重新部署owner67b3。

Main10六帧--dmabuf首次试验exit1：source change成功，P010 S_FMT接受（fourcc30313050、320x256、stride768、size294912），但crop320x256与向量320x240不符，拒绝在capture STREAMON前继续；输出STREAMOFF0/CLOSE_DONE。没有硬件帧验证通过。runner新增G_SELECTION_BEFORE_FORMAT日志，重新编译decode-test-crop作第二次有界试验（不重启PAS）：**切换格式前crop0,0,320,240，S_FMT(P010)后变0,0,320,256**，同样干净关闭。确认是vdec_s_fmt无条件inst->crop=对齐后width/height覆盖，不是固件最初报错；现有严格尺寸检查保持。

完整两轮结果和native-start已取回out/video-decode/hevc-main10-vector/sq2-p010-{main10,crop}-result.txt等，collector已取回（收尾需核对），发现退出后的invalid session/no valid instance消息（pkt221007/21001）；CLOSE_DONE只证明用户态关闭返回，不证明固件无迟到消息。当前boot不继续帧测试，后续用修复候选独立新启动。下一步修改SQ2捕获格式切换：source change后仅在存储尺寸不变且已有crop有效时保留固件crop，保留初始化/其它平台原语义；增加真实helper边界检查、新stage编译/符号审计/打包，再独立重启验证P010六帧。不能在驻留模块上直接热替换，也不能用忽略crop/换256高向量代替修复。

**2026-10-30 P010一次性启动已发出**：SSH自行恢复，仍boot59af714a…/guardY。重新核对BC711/ESP UUID及marker/data UUID/root backing，ESP余905132KiB。候选已上传临时路径并在设备sha1808a39f…及gzip -t通过，sync后原子改名/media/esp/boot/sq2-android1/native-c2-capabilities-p010.cpio.gz。仅实验入口initrd路径由v3改为p010，默认正常入口不变；菜单备份grub.cfg.before-p010（69e016c5…），新菜单d47afa2057dbd15797ecf296aa191e940d9ce39a9111146f76b5f88224c2b867。once=0备份sq2-c2-test.env.before-p010，上传once=1核验d762d0a0…后原子替换并sync。已通过Android reboot发出主动重启（exec session4758），尚待新boot确认，不重复发reboot。

重新编译的静态Main10 runner out/video-decode/hevc-main10-vector/decode-test-p010通过QEMU padded-reference（2字节/sample），已上传Android/data/local/tmp同名，尚未执行。新boot先验证core eb2a8725…/dec ce3366cd…、boot_completed和screen_only，再启动独立collector并仅一次video-decoder-lab-start；不可沿用上一boot的PAS状态或热卸。随后有界六帧Main10实际CRC/EOS/关闭，再做NV12回归。Android服务重启后回到镜像旧版，RAM owner/private-wrapper需要后续重新部署，不假定67b326ca仍在运行。

**2026-10-30 P010候选打包完成，未部署**：有效stage仍/root/.cache/sq2-video-p010-v3/venus；core eb2a8725c3dd82ab168dfa668fc4871d4e5feae07ac5b437480bbe62a046e8c4，dec ce3366cd67a9f7e4b16c2b875475fd0d2656c5fb4be242adb25ce55244450301。从当前设备新采集guardY+kallsyms，audit-sq2-video-modules.py排除被替换resident模块旧exports后，两模块missing=0；这仅符号闭包，不是ABI/硬件证明。实际helper ASan/UBSan六codec格式选择与默认索引检查再次通过。

pack-sq2-video-capabilities.py新增显式--revision p010及上述固定hash，保留1/3选择；新私有产物out/video-decode/boot-candidate/capabilities-p010.cpio.gz SHA1808a39f88c1f67984ad6770070194e178221eca6bf9711d50c85d1ba60ccb3a。完整解压/last-entry-wins审计只变core/dec/SHA256SUMS，密码addon最后，Android owner不变。尚未上传、改菜单或重启。设备曾成功读取BC711/1000215216sectors、ESP UUID B027-0512/marker SQ2-INTERNAL-512-20261027、data UUID4f46d2ec…、loop0 backing p3、root erofs/data p1、菜单hash69e016c5…和once=0；紧接着df及SCP读取连接超时，不能把早先成功当作部署完成。grub.before-p010.cfg不存在，后续生成菜单因缺输入退出，没有任何设备写入。下轮先复查连接/状态再做部署前身份核对。P010原生向量尚未实机，不能广告Main10。

**2026-10-30 VP8 4K30普通应用通过**：同boot59af714a…/owner67b326ca…，vp8-4k自动选择c2.sq2.vp8.experimental，hardware/format_supported=true；3840x2160/30、300输入/输出/渲染，9962ms，EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED。目录已取回out/video-decode/vpx-app/sq2-player-vp8-4k-59af714a-4cfe-4017-9ce0-d1656715c923。构建session45611已完成，当前APK为VP8 4K30。该轮沿用仍在120秒窗口内的sq2-vp9-4k60-kernel.log（测试结束时uptime约925秒，collector起点约824秒），现已取回完整日志，KMSG_END timeout120000ms、245records/17664bytes；无KMSG_OVERRUN，未见venus/hfi/BUG/Oops/panic记录（有audit限流，不能据此推断所有日志无缺失）。收尾XML_REMOVED、服务running。不测试VP8 4K60：32400宏块/帧乘60超出固件1036800宏块/秒；不是把VP9的吞吐复制给VP8。接下来优先补P010原生候选启动/10bit和剩余MPEG适配，正常启动持久化仍未完成。

**2026-10-30 VP9 4K60普通应用通过**：同boot59af714a…/owner67b326ca…，profile0 3840x2160/60普通UID自动选择c2.sq2.vp9.experimental，hardware/format_supported=true；600输入/输出/渲染，10056ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。完整结果/截图已取回out/video-decode/vpx-app/sq2-player-vp9-4k60-59af714a-4cfe-4017-9ce0-d1656715c923，人工检查late截图有测试图案；不是逐像素校验或长片稳定性认证。收尾boot不变、XML_REMOVED。上一轮4K30 collector已确认KMSG_END timeout120000ms/255records/18171bytes；4K60独立collector的smoke已验证，120秒结束后仍需取回完整日志。60fps构建session82952已正常完成；当前APK为VP9 4K60。下一项VP8 4K30构建正在运行（核对当前会话），还未实机运行。

**2026-10-30 VP9 4K30应用通过、新owner VP8回归通过**：boot仍59af714a…，owner67b326ca…，普通UID1010254自动选择硬件组件。vp8-small3（320x240/30）与vp9-4k（3840x2160/30、VP9 profile0）均300输入/输出/渲染、9963ms、EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。结果和截图已取回out/video-decode/vpx-app下各同名boot目录；收尾XML_REMOVED、服务running、boot未变。4K结果不是60fps、10bit或长片证明。独立sq2-vp9-4k-kernel.log有smoke标记，120秒有界collector，完整收尾仍需取回核对。下一项VP9 4K60 APK构建已启动，exec session82952，输出player-app-3840x2160-vp9-60fps；先轮询现有handle，不重复启动构建。尚未上传或测试60fps APK。调试screen_off_timeout再次实读2147483646、guardY。

**2026-10-30 新用户偏好与VPx普通应用通过**：用户已移除PIN，明确调试期间可跳过锁屏，并要求不自动关屏。已在新boot以am switch-user 10、WAKEUP、wm dismiss-keyguard进入用户10，不清除凭据。screen_off_timeout设2147483646ms（约24.8天），避免旧lab-keep-awake仅对2147483647的两分钟迁移；复查值保持。覆盖此前30分钟调试偏好；手动关屏和suspend.screen_only=Y不变。用户还明确固件不支持就丢弃，见能力文档。

此前c939c2e已提交并推送origin/codex/sq2-video-decode，随后确认正常bootd4bb6f99…/boot_completed1/无native。完整复核NVMe/ESP/root/菜单69e016c5…/v3镜像72a6037f…后仅置一次env并主动重启，当前boot **59af714a-4cfe-4017-9ce0-d1656715c923**，env已清0、core50afdd91…正确。独立collector smoke后本boot仅首次lab-start成功，不能重跑或热卸。

新四组件owner372c024b…先通过HEVC4K60普通APK回归：600输入/输出/渲染，9977ms，EOS/STOP/RELEASE/RESTORED。VP8首轮测试APK因WebM没有KEY_FRAME_RATE而NPE，未进入硬解；build脚本现生成fps.txt，APK验证容器若有帧率则与固定测试元数据一致，否则采用元数据，禁止猜实际文件帧率。vp8-small2自动选择hardware=true，300帧9965ms、EOS/STOP/RELEASE通过。

VP9首轮自动选中但9字节CSD被组件拒绝；对照本地固定AOSP C2SoftVpxDec，改为有界消费VPx容器CSD，不拼接到硬件帧：拒EOS、多buffer/多block、映射错误及超过1MiB。新owner **67b326ca19b2c949078ba6bcdb13926db5e0a214f1c1c1775a40f26673446c44** 已RAM部署（前版备份sq2-c2-owner-vpx-before-csd，旧双组件备份sq2-c2-owner-before-vpx）。vp9-small2普通UID自动选择hardware=true/format_supported=true，300输入/输出/渲染9969ms，EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED。VP8/VP9均320x240/profile0，不是4K/10bit/长片认证；VP8通过版本为CSD改动前，后续需回归新版本。

本轮启动日志、完整APK结果/截图/日志和collector取回out/video-decode/vpx-app；collector确认KMSG_END timeout120000ms，收尾XML_REMOVED、boot保持。当前APK为修复帧率的VP9版。默认启动镜像不变，新owner/private wrapper仅RAM；无新MPEG2或P010硬件测试。此次提交后的源码/文档改动尚未提交。

**2026-10-30 会话层P010适配，仅主机测试**：Sq2V4L2Session::open新增末尾outputFormat参数（缺省NV12，所有旧调用保持），P010仅允许HEVC/VP9；Sq2Geometry新增实际format，捕获协商核对返回fourcc不可静默降8bit。可见尺寸/字节stride/sizeimage采用64bit边界计算，拒绝奇数YUV420尺寸、P010奇数字节stride、短分配和短输出payload。单帧16MiB/总192MiB限制未扩大，4K P010仍不接受。Android C2未请求P010，图像复制与profile广告仍8bit。

tests/sq2_v4l2_session_test.cpp在ASan/UBSan下覆盖NV12/P010两组payload/token/EOS、replacement失败后客户端引用保留、100帧复用5次分配、close/release；额外拒绝错误stride、驱动替换格式、47B短分配和47B短帧，并检查零交付/无映射泄漏。四组件store重新编译通过，out/aidl-store/interface-test已被新版覆盖，**设备上的2bea9691…候选仍旧，不能把新本地产物与旧哈希混用**。没有上传或替换设备服务。SSH仍超时，正常重启是否完成未知，仍待用户屏幕状态回应。

**2026-10-30 P010候选实际helper离线检查通过**：新增tests/test_sq2_p010_selection.py，直接提取stage中to_hfi_raw_fmt/find_fmt_from_caps/venus_helper_get_out_fmts/get_framesize_raw_p010实际C代码，使用已保存固件各codec格式表，在ASan/UBSan下执行。HEVC/VP9 P010选择TP10_UBWC DPB+P010 OPB通过；其它四codec拒绝；删除任一必需端口格式后拒绝且输出清0；六codec NV12选择保持UBWC+NV12。并检查数组默认NV12索引0/AVC索引8保持、P010末尾追加。仅主机算法检查，不证明内核ABI或硬件帧。

尺寸函数实际计算320x240 P010=294912B，3840x2160 P010=25067520B，确认当前单帧16MiB上限不足。扩展Android路径必须同时覆盖native capture、C2GraphicBlock、私有mapper/import缓存预算，不能只改XML或单处上限。设备本轮SSH仍超时，未操作设备或重复重启；仍等待用户屏幕状态/恢复信息。此轮有离线验证进展，目标未标blocked或complete。

**2026-10-30 P010原生候选，仅离线**：prepare-sq2-venus-iris1.py新增V4L2 P010捕获格式，仅SQ2且已识别10bit时可选/枚举；追加到原format数组末尾，保留硬编码AVC索引8/NV12索引0。to_hfi_raw_fmt补P010映射，bytesperline按ALIGN(width*2,256)；复用内核已有P010 sizeimage计算。SQ2 P010输出必须真实能力同时包含OUTPUT TP10_UBWC和OUTPUT2 P010，才选择该DPB/OPB组合，否则EINVAL，避免原通用逻辑错误寻找P010_UBWC或把P010输出当成OUTPUT。其它平台不暴露新格式。

有效stage为/root/.cache/sq2-video-p010-v3/venus，编译通过（仍缺Module.symvers、需未来实机导出审计）；较早p010和p010-v2因插入格式改变默认索引均废弃，禁止部署。没有打包/上传。原生runner现在从header每sample字节数选择NV12/P010、检查实际返回fourcc、按字节行宽/stride计算CRC及缓冲边界。--reference-test把独立参考放入带0xa5填充的stride/scanline缓冲再校验，AArch64 QEMU下NV12/P010六帧均通过；test_sq2_v4l2_runner.py资源/阶段故障回归通过。Main10二进制out/video-decode/hevc-main10-vector/decode-test尚未实机执行。

设备SSH本轮仍超时，屏幕状态问题待用户回复；未重复reboot/写盘。下一步恢复设备后先评估MPEG2会话诊断与候选启动，再验证P010真实geometry/帧字节/生命周期；Android Sq2V4L2Session/C2GraphicBlock仍仅8bit，单帧16MiB及192MiB预算尚未覆盖4K P010/8K，不得直接广告Main10或放大上限。

**2026-10-30 离线Main10验证素材**：设备重启后SSH仍超时，用户屏幕状态问题尚无回复，未重复重启/写设备。离线检查MPEG2固件能力明确含output2 NV12，故不能仅因0x1008推断该格式不受支持；具体属性还待first-error跟踪。现有is_dynamic_bufmode对HFI4默认true、multistream/format选择等路径已检查，尚无证据选择修改哪项。

build-sq2-hevc-test-vector.py新增--bit-depth 10，生成10-bit geq图样（非8-bit源简单扩展）、Main10 ES和独立P010LE六帧参考；ffprobe校验profile/pix_fmt，P010低6位必须全0且有效10bit低2位确有非零。out/video-decode/hevc-main10-vector码流SHAd47f1b74d9e93b6a2e70ad3b60fd1bef577e7850ea84f54d59a362cf19e6454b。8-bit重新生成与旧clip/ref逐字节一致。header新增每sample字节数；NV12原生runner编译时明确拒绝2字节参考，避免误测。**没有实现或部署P010输出，Main10未硬件验证**；下一步仍需设备恢复及原生10-bit格式/geometry/buffer适配，不能改XML直接宣称支持。

**2026-10-30 MPEG2错误洪泛定性与正常启动恢复**：只观察既有流量的sample-hfi-events.sh运行1秒、64KiB独立trace缓冲，未提交任何解码命令。当前持续消息确为size28/type0x21001/session0x3013efa5/event2/data1=0x1008/data2=0，即HFI_ERR_SESSION_UNSUPPORTED_SETTING。固定hfi_msgs.c将此类列为非fatal并清inst->error，故此前没有直接上报错误；尚未确定是哪项属性、不证明硬件不支持MPEG2。样本已取回out/video-decode/mpeg2-vector/sq2-hfi-existing-events.txt，探针已移除。

为终止坏会话持续事件，确认一次env=0后sync并主动Android reboot，目标为未改动默认正常入口（不载native）。重启SSH已reset，后续连接仍超时；**本条记录时尚未确认新boot/恢复，不要重复发reboot**。旧boot3dd93e6c…不再当当前状态。待SSH恢复先确认boot ID、boot_completed、guard和native是否未加载，再安排独立候选启动，不在默认入口直接绕过lab-start条件。

新增sample-mpeg2-first-error.sh，仅语法检查，未部署/执行：新启动native和独立collector就绪后，一次320x256测试前注册property提交及HFI event探针，首个event2/data1=0x1008触发traceoff，保留前序属性；128KiB/45秒timeout，退出移除探针。这是诊断候选，trigger实际支持及清理需核对，不能把脚本准备视为定位完成。MPEG2原320x240裁剪问题仍开放，Android应用解锁问题继续待用户响应。

**2026-10-30 MPEG-2验证失败，当前native状态需重新评估**：新增build-sq2-mpeg2-test-vector.py，生成六帧无B帧MPEG2 ES，ffprobe包边界覆盖全流/每包一picture，独立NV12参考和CRC。支持320x240及320x256，直接核对sequence header尺寸。runner新增SQ2_MPEG2_VECTOR，不放宽geometry判定。原320x240实测收到crop320x256，在输出前拒绝，streamoff/close正常、exit1。独立sample-mpeg2-geometry.sh有界kprobe确认vdec_event_change error0,width320,height256,input_crop全0；这是固件报告缺少可见裁剪信息，不是测试素材尺寸错误。探针已退出移除。

320x256对照保留原失败，不作为缩小支持范围的替代。对照完成source change和capture STREAMON，但0输出/0输入归还，RESULT ret1；两个STREAMOFF返回0，CLOSE_BEGIN之后外层25秒timeout终止进程，exit143，无CLOSE_DONE。之后内核大量`no valid instance ... pkt:21001`，不据此断言整板崩溃或PAS停止。boot仍3dd93e6c…、guardY、SSH可用、无mpeg2探针进程。**不要继续沿用当前native做其它解码回归，也不要热卸/重跑lab-start；先分析保存的失败/关闭证据，必要时干净启动再测。** 正常默认启动镜像未改，临时四组件服务尚未部署。

原始输出/geometry trace/完整事后dmesg已取回out/video-decode/mpeg2-vector/sq2-mpeg2-*.txt。大量旧会话消息可能已挤出更早dmesg，后续需新独立有界collector覆盖完整测试过程，不能用事后空error筛选断言无错误。当前user0，普通应用解锁问题仍待用户回复；MPEG1/2、10-bit及完整能力目标未完成。

**2026-10-30 VP8/VP9 Codec2组件像素验证通过**：在boot3dd93e6c…驻留v3后端运行独立Android C2 harness，无需桌面、不注册新store。build-sq2-c2-component-test.py新增--codec vp8/vp9，输入为各自六帧原生向量；组件和接口使用与候选store相同Sq2Codec映射。测试按C2GraphicView实际rowInc/colInc重组规范NV12再比较，兼容I420/NV12/NV21布局，避免旧测试直接把U平面当交错UV读取的错误。

两个格式均C2_RESULT done7/mask3f/eos1/errors0，六帧Y/UV CRC全匹配独立FFmpeg参考，queue/stop/release返回0，客户端保留buffer最后释放，进程exit0。AVC同源码回归也done7/mask3f/eos1/errors0、停止释放0。日志out/video-decode/vpx-component及设备/data/local/tmp/sq2-c2-{vp8,vp9}-frames-result.txt、sq2-c2-avc-vpx-regression.txt。此为独立root进程的真实C2组件→原生硬件→图像buffer验证，**不是普通UID MediaCodec/Extractor/Surface播放证明**。当前服务owner739d…仍不变，四格式candidate未--serve；user0仍待解锁，应用APK未替换。无需为此重启或重新启动native/PAS。

**2026-10-30 Android VPx接入候选（未替换服务）**：新增sq2-codecs.h集中映射AVC/HEVC/VP8/VP9名字、MIME和fourcc，接口/组件改显式Sq2Codec，默认AVC供旧测试使用。store新增VP8/VP9条目，临时XML新增OMX.qcom.video.decoder.vp8/vp9别名；当前输出仍NV12 8-bit，VP8 profile0/LEVEL_UNUSED，VP9 profile0、level1..5.1，完整固件profile2/尺寸吞吐仍未实现。VPx拒绝意外codec-config work，避免拼接成损坏帧；真实WebM MediaExtractor路径尚待验证。

四组件候选SHA2bea96916fcd2bcdd6ed5491a93e8e49cd39307054efd8ef8e77e0201ae527ab，设备/data/local/tmp/sq2-c2-vpx-candidate本地store检查STATUS0/list4及MIME正确，未--serve、未替换owner739d…或当前服务。build-sq2-c2-aidl-service.py新增--interface-test输出parameter-test，设备通过VP8/VP9 profile0接受、未支持profile拒绝并保持原配置，以及AVC/尺寸/pool原回归。编译和参数检查不等于解码通过。

build-sq2-video-app.sh新增vp8/vp9，生成10秒300帧WebM（保留历史资产名clip.mp4，Extractor按内容识别），lag/altref关闭；MainActivity按四种MIME核对硬件组件，runner新增vp8-small/vp9-small自动选择标签。两个320x240 APK已构建，未安装，设备当前仍旧HEVC4K60 APK。下一步解锁后先做v3 HEVC4K60回归，再备份/替换新owner、上传新XML/runner、分别安装VPx APK实测，不能以store枚举替代播放。当前user0，解锁问题仍待用户回答，不重复请求、不清凭据。

v3 collector最终KMSG_END timeout120000ms已确认并完整取回caps-regression/sq2-caps-v3-kernel.log；native保持驻留，当前boot3dd93e6c…。

v3本轮收尾更新：已核对userdata保留的wrapper bdf11d60…、owner739d346e…、私有mapper eb31d402…及全局mapper c1a57082…，在RAM恢复私有C2 wrapper并启动，service running。当前前台user0，已请求用户正常解锁进入桌面以继续Android回归，未改凭据。下一轮可用新boot尚未占用的hevc-4k60标签；native已驻留，禁止重跑lab-start。

**2026-10-30 v3实机VP9/尺寸枚举通过**：再次完整核对BC711512GB、ESP B027-0512、SQ2-INTERNAL-512-20261027、userdata UUID和root loop0/p3，ESP可用1098868KiB后上传独立native-c2-capabilities-v3.cpio.gz。设备完整SHA72a6037f…、gzip通过，sync/原子替换；备份grub.before-capabilities-v3.cfg及sq2-c2-before-v3.env。菜单仅实验initrd改v3，新菜单SHA69e016c5…，默认正常入口保持。一次env1在启动中已消费为0。

主动Android reboot后boot **3dd93e6c-cd92-4f9e-8c62-ca23bebd3fd1**，boot_completed1/guardY，core50afdd91…/dec257ffcd1…匹配。独立120秒collector smoke后仅首次调用video-decoder-lab-start，START_EXIT0、native ready；不热卸载。VP9六帧Y/UV CRC全匹配，EOS flags104008已无ERROR，RESULT frames6/mask3f/input_returned6/drain1/last1/ret0，streamoff和close通过。HEVC六帧同样完整通过。全格式新静态探针实读MPEG2 96..1920、VP8 96..4096、AVC/HEVC/VP9 96..8192，均step1。证明按codec查表生效，不是任意方形最大尺寸或吞吐实测。VP9/VP8 profile controls仍过宽0..3，HEVC/MPEG2查询EINVAL，MPEG1缺枚举，这些仍需对齐。

注意out/video-decode/sq2-v4l2-capabilities是旧Android动态、仅AVC探针，不能再用作全格式证据。本轮从当前源码重新静态编译caps-regression/capabilities-v3-probe，完整枚举结果sq2-caps-v3-enumeration-all.txt。启动/VP9/HEVC/枚举日志已取回out/video-decode/caps-regression。collector最终日志尚需取回。当前启动C2仍镜像旧版，新owner/private wrapper尚未在本boot恢复；不能将本轮原生验证当作Android v3回归。后续恢复C2、接入其它codec/10-bit/真实联合能力并持久化，目标未完成。

**2026-10-30 VP9空EOS候选v3（尚未部署）**：sample-vp9-eos.sh新增packet模式，实际hfi_session_ftb_done包size104/stream1/error0/flags0x20000021/bytes0/offset0；六帧各147456B且error0。证据out/video-decode/caps-regression/sq2-vp9-eos-packet-{trace,decode}.txt。原生runner仍ret1，未伪造成功。venus-sq2-vp9-eos.h仅对SQ2 VP9 decoder、error0、bytes/offset0、精确EOS|SYNCFRAME|DROP_FRAME组合清DROP；其它SoC/codec/encoder、任意非零error/长度/偏移及其它flags全保留。实际helper ASan/UBSan测试覆盖上述各字段32位单bit变化，通过。prepare脚本仅decoder packet分支调用，vdec原有损坏/丢帧处理不变。

stage /root/.cache/sq2-video-caps-v3/venus编译完成，含v2逐codec尺寸枚举。core SHA50afdd9164eb9eb4c2f7882871045dcdd52c88ef0f32e219ab875c6251ea644b，dec SHA257ffcd19294a3a16ddb667a20ed3ae0196471b206de947c4ed8e1f0fb585d92。audit-sq2-video-modules.py现剔除被替换resident模块的旧导出再核对新模块闭包，避免误用旧符号；当前实机导出审计missing0，报告out/video-decode/caps-regression/v3-link-audit.json。缺Module.symvers的modpost警告仍存在，此为符号闭包不是完整ABI验证。

pack-sq2-video-capabilities.py --revision 3生成capabilities-v3.cpio.gz SHA72a6037f63400e804e031d2b2d057d09b8d7e38017ea86ad6e50cd037b3fff16，审计仅core/dec/SHA256SUMS三项改变、密码addon最后。尚未上传ESP/改菜单/重启；当前仍boot9e548e25…第一版native及临时新版C2。下一步重新核对物理盘/ESP/root与剩余空间，独立一次性候选启动验证VP9 EOS及逐codec枚举，不能热卸native/PAS。Android新增格式、Main10与完整能力对齐未完成。

**2026-10-30能力候选帧回归**：同boot9e548e25…，新native后端HEVC六帧独立NV12 CRC、EOS/streamoff/close通过。RAM恢复私有wrapper bdf11d60…、owner739d346e…及192MiB mapper eb31d402…，不是镜像持久化。用户正常解锁后前台user10，普通APK hevc-4k60b自动选中c2.sq2.hevc.experimental，hardware=true/format_supported=true；3840×2160/60fps/Main，600输入/输出/渲染，9977ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。此前hevc-4k60c因user0禁止启动失败，不能计为解码失败。收尾XML_REMOVED、media running、guardY、boot不变，独立collector已KMSG_END timeout120000ms。证据完整取回out/video-decode/caps-regression。仅合成10秒8-bit验证，非Main10或B站4K证明。

VP9同boot重现六帧像素匹配但空EOS ERROR。新增sample-vp9-eos.sh以独立tracefs instance、128KiB缓冲、25秒timeout采集vdec_buf_done标量，不卸载模块。实际EOS为type3/bytes0/offset0、V4L2 flags0x100008、HFI flags0x20000021，即EOS|SYNCFRAME|DROP_FRAME，没有DATACORRUPT；当前驱动DROP_FRAME分支将其标为VB2错误。尚未修改或吞掉此错误，需进一步明确空结束标记与真正坏帧的区分及固件packet error。探针和instance退出均已移除，原始trace/decode日志留out。第二版逐codec ENUM_FRAMESIZES候选仍未部署；后续其它codec、10-bit、完整能力映射及持久化仍未完成。

**2026-10-30能力候选实机启动**：备份EFI/BOOT/grub.before-capabilities.cfg和sq2-c2-before-capabilities.env，仅实验项initrd改为native-c2-capabilities.cpio.gz，菜单新SHA8dbc5acd…；默认正常项不变。一次env=1已消费为0。主动Android reboot进入boot9e548e25-71bc-4e43-9e6b-90e01a5e0853，boot_completed1/guardY，新core39400063…匹配。独立120秒collector smoke后只调用一次video-decoder-lab-start，所有哈希通过、START_EXIT0、/dev/video0就绪，PAS/native保持驻留，不热卸。

首次枚举AVC尺寸已变8192，表明真实能力路径生效；但MPEG2/VP8也显示8192。源码确认vdec_enum_framesizes忽略请求fourcc而查询当前默认AVC能力，因此此前“所有codec原生4096”不能当作每个codec真实上限。新增venus-sq2-frame-sizes.h，仅SQ2压缩格式枚举按codec查表、无会话状态修改；MPEG2/VP8/AVC/HEVC/VP9分别1920/4096/8192/8192/8192，缺尺寸的MPEG1返回EINVAL而不捏造。实际函数与恢复能力包ASan/UBSan测试通过，第二版stage/root/.cache/sq2-video-caps-v2/venus编译通过，**尚未打包/部署第二版**。现机仍第一版core/dec。

本轮证据out/video-decode/hfi-session/sq2-caps-9e54-{start.txt,enumeration.txt,kernel.log}，collector最终KMSG_END timeout120000ms已取回。Android旧启动C2服务仍为原基线，上一boot临时mapper wrapper未恢复；/data/local/tmp新owner/mapper文件仍可用，但不能把当前启动记为新版Android解码回归通过。下一步先完成新native后端帧回归/逐格式枚举修复验证，再恢复新版私有C2运行态。

**候选上传完成**：/media/esp/boot/sq2-android1/native-c2-capabilities.cpio.gz已完整落盘198373763字节，SHA1cb9fa4b…与本地一致，设备gzip -t通过；sync后从.new原子命名，再sync。返回CANDIDATE_STORED_MENU_UNCHANGED。未更新菜单/一次标记、未重启，下一步备份当前EFI菜单与env并准备一次性能力启动。VPx collector已确认KMSG_END timeout120000ms，最终记录尚在设备。

**2026-10-30 VP8/VP9原生验证与磁盘复核**：新增build-sq2-vpx-test-vector.py生成6帧VP8/VP9，剥离IVF容器后提交真实帧包，独立FFmpeg NV12参考；runner用SQ2_VPX_VECTOR选择实际fourcc。两者QEMU CRC自测通过，主机runner故障清理回归通过。实机同boot39441275…：VP8六帧Y/UV全匹配、EOS/streamoff/close、exit0；VP9六帧也全匹配，但末尾空缓冲flags104048包含ERROR，runner未吞错，ret1/last0，streamoff/close正常。暂不可标VP9完整成功；需进一步取得HFI flags/error来源。日志out/video-decode/{vp8-vector,vp9-vector}，collector bounded120秒，尚需取回最终KMSG_END。未改当前resident native。

磁盘身份现已完整只读复核：BC711 SK hynix512GB、1000215216扇区；标记SQ2-INTERNAL-512-20261027；ESP /dev/nvme0n1p6 UUID B027-0512；data p1 UUID4f46d2ec-323e-486e-b473-fc2adc0849ba；Android root /dev/loop0 erofs来自p3、data来自p1。ESP可用1292604KiB。真实菜单/EFI/BOOT/grub.cfg默认sq2-internal，实验sq2-c2-test由sq2-c2-test.env先消费一次标记后启动，旧实验镜像SHAa5038428…匹配本地基线。能力候选正在上传独立native-c2-capabilities.cpio.gz.new；必须核对完整hash/gzip后才原子命名、更新一次性入口，默认镜像/菜单尚未修改，未重启。

**2026-10-30能力候选打包/链接检查**：现场仍boot39441275…、guardY、XML_REMOVED。新venus-core required177/provided67，新venus-dec required130；从实际__ksymtab集合剔除resident旧core/dec后，按新core导出再核对decoder，missing均0，vermagic均6.3.8-sq2-android1 SMP mod_unload aarch64。证据out/video-decode/hfi-session/candidate-link-audit.json。此为符号闭包，不是加载/解码验证。

新增pack-sq2-video-capabilities.py固定a5038428…实验基线及本轮两个模块哈希，生成out/video-decode/boot-candidate/capabilities.cpio.gz SHA1cb9fa4b2759b9545220d0192db8f55cad591542cb93f2c83e5e78021ab3214f。最终条目审计只变/etc/sq2-video-native下core/dec/SHA256SUMS三项，凭据addon最后，gzip测试通过。旧启动C2服务刻意保留；新owner739d346e…和私有mapper后续仍需在启动后RAM部署，不能据此镜像宣称完整Android硬解持久化。

未写ESP/菜单、未重启。只读已确认Surface NVMe为BC711 SK hynix512GB、ESP挂载/dev/nvme0n1p6、存在SQ2-SSD-ID/SQ2-PROBE-MEDIA标记；后续标记内容/UUID/菜单读取遇SSH超时，磁盘身份门槛尚未完整。不要凭此半份检查进行磁盘写入。接下来复核标记/UUID/root source/可用空间/真实EFI菜单位置，再准备一次性启动及原入口回退；不要热卸当前native/PAS。

**2026-10-30 SQ2真实能力解析候选**：新增sq2-hfi-properties.h有界逐属性walker及venus-sq2-capabilities.h事务式目标表构建。仅SYS_INIT_DONE且IS_SQ2_HFI走此路径，传入pkt->num_properties校验完整属性数；session初始化及其它平台保留原路径。临时堆表全部验证后才发布，拒绝未知ID、截断/计数溢出、目标数组累积越界，失败不改变原core能力；不把payload内碰巧等于属性ID的值当成新属性。prepare-sq2-venus-iris1.py增加固定hfi_parser.c哈希及这些补丁。

实际C代码ASan/UBSan验证恢复的75属性包、全部截断长度、payload ID碰撞、极大计数、非法plane、未知ID、33项超限和两组17项累积越界。真实表解析10个编码/解码条目，decoder掩码0x701a，各格式宽度8192/4096/1920正确；失败事务旧表逐字节不变。候选stage /root/.cache/sq2-video-caps/venus，内核modules编译通过，但缺Module.symvers、KBUILD_MODPOST_WARN=1，尚未做本候选运行导出闭包/实机加载，不能视作部署通过。现机native/PAS保持驻留；后续需独立启动验证，不热卸换core。其它codec接入、MPEG1格式映射、10-bit及更大缓冲仍待完成。

**2026-10-30能力来源恢复/定位**：SSH恢复，同boot39441275…、XML_REMOVED。hevc-4k60c完整目录与collector取回，600帧9978ms结果再次核实；copy+sync2297973us，释放7项87822336B，collector KMSG_END timeout120000ms。全格式只读工具实机枚举MPEG2/AVC/VP8/VP9/HEVC、均96..4096，缺MPEG1；HEVC/MPEG2 profile control EINVAL。原始/data/local/tmp/sq2-hfi-session-44c0.kmsg已恢复，严格解析75属性/774体字全部通过。数值快照configs/sq2-firmware-decode-capabilities.json，明细见VIDEO-CAPABILITY-ALIGNMENT.md。MPEG2固件上限1920/30fps而非当前驱动4096，AVC/HEVC/VP9固件8192，不能统一扩大所有codec。

源码定位：固定6.3 hfi_parser先hfi_platform_parser，成功便返回，HFI4使用hfi_platform_v4静态表而未解析真实SYS_INIT能力。SQ2应接有界逐属性解析，保留其它平台行为；不能简单绕过静态入口后落入无长度边界的逐字扫描。当前只完成定位/证据快照，尚未修改/加载native能力解析。native/PAS保持驻留，切勿为此热卸载。下一步可先做解析器与主机边界测试，然后准备独立启动候选；并继续其它codec的实际帧验证。

**2026-10-30 最新目标与运行态**：用户明确要求对齐固件上报的全部解码格式及分辨率，包含HEVC 4K60；不能只补AVC/HEVC名字。已有记录包含H264、HEVC、VP8、VP9、MPEG1/2，需逐格式核对profile/位深/尺寸/吞吐和Android映射。固件报告8192范围而现V4L2枚举只到4096，是待解决差异，不应静默当成对齐。当前原始caps-44c0.json不在本工作站预期目录，详细分组能力需要恢复原始证据后再填。

**更新：HEVC 4K60已通过首轮**。192MiB native capture预算候选实际上传哈希739d346e5ef12949a39e41a8b827c1fe3697eafa16eb865fd0c3a9ffe6f30c87，与本地一致，已替换RAM owner并启动。hevc-4k60c自动选择hardware=true/format_supported=true，3840×2160/60fps/Main/600帧全部输入、输出、渲染，9978ms，EOS/realtime/pass=true及STOP_OK/RELEASE_OK/RESTORED。不是Main10、长片或B站4K证明。运行完整result已由SSH返回；之后取回截图/日志遇网络超时，需补取/data/local/tmp/sq2-player-hevc-4k60c-39441275-d887-4e17-a774-9e1fad93cdd3及collector文件。以下“尚未上传”描述为本轮较早状态。能力对齐表见VIDEO-CAPABILITY-ALIGNMENT.md，原生能力枚举工具已扩为ENUM_FMT全格式，当前只有本地构建通过，上传也遇SSH超时。

本轮Bilibili HD 2.0.1实接通：c2原名评分100被拒，OMX.sq2别名700、720p约90秒2716帧。按用户要求改OMX.qcom.video.decoder.avc后评分800并选中原c2.sq2.avc.experimental；1080p capture及2073600像素HW资源，最后会话1384帧，退出释放12项37748736B。BV1NGZtBwELa画质菜单仍最高1080P高码率。qcom轮完整证据out/video-decode/bilibili/sq2-bilibili-qcom；测试退出OBSERVED/RESTORED、XML_REMOVED、media running、guardY。别名不是OEM驱动替换，未写默认镜像。

HEVC已取得原生像素证据：build-sq2-hevc-test-vector.py生成320×240 Main、6帧、无B帧；原生runner编译SQ2_HEVC_VECTOR，真实6帧Y/UV CRC全匹配独立FFmpeg参考，EOS/streamoff/close及返回0。Android store新增c2.sq2.hevc.experimental，XML别名OMX.qcom.video.decoder.hevc，暂仅Main 8-bit；配置拼接复用原有Annex-B有界字节容器（类名Sq2AvcConfig不表示解析AVC）。小尺寸HEVC含219个B帧，自动选择hardware=true、300输入/输出/渲染、9961ms、EOS/STOP/RELEASE/RESTORED全通过。

4K60 Main素材600帧、449个B帧、约24.8Mbps/level5.1。前两轮在开始后失败；加入SESSION_ERROR日志确认errno=-75/EOVERFLOW，未见输出。当前候选将native capture预算128MiB改192MiB，保留32槽/单帧16MiB上限以容纳HEVC额外参考槽，已构建但上传遇SSH超时，尚未验证生效。当前RAM运行的是带错误日志的候选；原AVC owner9a556a…保留/data/local/tmp/sq2-c2-owner-avc-9a55；首版HEVC owner84781d97…通过小尺寸。下一步必须核对上传哈希再运行hevc-4k60c，不可把未上传的预算修复标为有效。默认镜像未改，boot39441275…，native/PAS不重复装卸。

新APK构建支持SIZE hevc 60，期望600帧/10秒；当前设备APK为4K60测试版。原生测试证据及两轮应用日志在out/video-decode/hevc-playback，测试脚本均撤销临时XML。主机runner测试移除对私有out向量依赖，ASan/UBSan阶段边界与故障清理通过。尚需HEVC Surface像素、4K60性能、AVC回归、其它格式、持久化及重启；目标仍未完成。

**AVC Main/High、B帧及4K自动选择通过（2026-10-29，最新）**：普通APK的1080p Main/High各300帧均完整输出/渲染、约9.96秒，PTS严格递增，EOS/STOP/RELEASE正常。High素材含141个B帧。新增--profile main/high六帧向量，ffprobe coded_picture_number建立解码序→显示PTS映射；High向量ce117e50…含3个B帧，Surface/CSD输出6帧逐字节等于独立FFmpeg NV12参考，修改缓存前后各通过一次。证据out/video-decode/high-bframes-vector/{frames,final-frames}。

4K High首轮4输入0输出：复制到V4L2后未归还框架输入缓冲，等待重排输出造成入口阻塞；process提交/泵完成后clear input.buffers（V4L2已有独立复制），新增C2PortActualDelayTuning::output=8。修复后指定组件300帧9960ms通过。随后接口发布Constrained Baseline/Main/High三项，High10仍拒绝；10参数、level5.1/High接受/High10拒绝及pool往返实机通过。自动选择首轮在第9个输出allocation触及旧8项import上限，错误返回NO_MEMORY；改有界LRU后完整300帧但14.899秒，缓存轮换拖慢，不记成功。

最终import和私有minigbm缓存协调为最多16项/192MiB、单allocation16MiB；import按最近使用淘汰，仍仅持独立AHB引用、不持queue block/CPU lock，超大或不可测大小跳过优化。主机实际unmap/destroy ASan/UBSan覆盖10000次复用、16×12MiB项数边界、12×16MiB字节边界及31次最终释放通过。新私有库eb31d4022c97e1ac70e4b396eeb7c5599d1f904730bbdd241704a6929bc967a8；旧b673保存在设备sq2-map-cache/libminigbm_gralloc-128.so。原stage/helper已更新，构建仍使用该固定stage；不要误把之前128MiB构建当当前版本。

最终自动选择4K High：hardware=true/software=false/profiles=3/format_supported=true，300输入/输出/渲染，9964ms，EOS/realtime/pass=true、STOP/RELEASE/RESTORED；9个唯一输出，结束释放9项112914432B，copy+sync1.621秒。当前RAM owner9a556a2c3ada89e53d476d7aa71d712ec52e265ae8b891d96d122379de7bc62b、wrapper bdf11d60…；全局minigbm仍c1a57082…。APK d076bfe3…为4K High版。日志成功和失败全取回out/video-decode/profile-playback；最终collector KMSG_END timeout120000ms、无运行collector，XML_REMOVED、boot3944/PID1978/2246/guardY保持。未更改默认启动镜像，native/PAS不重复装卸。

下一步：实际Bilibili接入、其它codec能力对齐及实验包持久化/重启回归。上述只是指定合成码流验证，非所有AVC工具/长片/最大重排深度认证；不能将完成AVC测试标为整个硬解目标完成。输出delay=8是当前有界实验值，尚未按SPS动态推导。设备NDK frames探针已换成High向量e3f88650…，后续比较必须用high-bframes-vector/ref.nv12，不能误用原baseline参考。

**自动选择4K30通过（2026-10-29，最新）**：普通APK通过MediaCodecList查询，hardware=true/software=false、Constrained Baseline profile一项、format_supported=true，findDecoderForFormat自动选中c2.sq2.avc.experimental；3840x2160/30fps/300帧输入、输出、渲染全部完成，9960ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。这仍是合成AVC测试，不代表Bilibili、Main/High、HEVC/VP9或默认启动完成。

首轮自动枚举在VideoCapabilities.updateLimits抛出Range上下界异常并触发JNI SIGABRT，仅测试APK崩溃；设备boot39441275-d887-4e17-a774-9e1fad93cdd3、框架PID1978/2246未变，用户确认屏幕可操作。接口补齐profile/level与input video/avc、output video/raw MIME。只公布Constrained Baseline、最高level5.2；起初仅接受5.2导致configure拒绝框架传入5.1，补齐标准较低level后通过。接口实机9参数、5.1接受、High拒绝、尺寸错误回退及block pool往返通过；不把level范围视作每档性能认证。XML实验尺寸更新到320x240–3840x2160。

当前RAM owner SHA516f682393e6db1fadaa17b800b730cc1fcefdeb8967036964efa417f047d73e，私有wrapper/mapper不变，全局minigbm SHA c1a57082…保持。新APK be5bcfee…支持codec=auto；runner新增auto标签并读取全部logcat buffer（此前默认buffer日志为空）。最新完整证据已取回out/video-decode/auto-selection/；collector smoke通过，显式停止后KMSG_END reason=signal/52613ms，XML_REMOVED、guardY、boot/PID保持。当前正常启动镜像未改，native/PAS保持驻留，不重复装卸。下一步真实应用与其它profile验证、补齐持久化包及重启回归；目标仍未完成。

**4K30同一会话60秒通过（2026-10-29，最新）**：测试APK loops=6重复10秒资产，仅推进输入PTS、不重建decoder/flush，1800输入/输出/渲染，EOS/realtime/pass=true，59961ms，STOP_OK/RELEASE_OK。始终4个唯一输出缓冲，结束IMPORT_RELEASE count4/50184192B，copy+sync6.196s（含DMA2.063s）。这是一分钟合成baseline测试，不是数小时稳定性/真实B站/其它profile/codec证明。当前APK SHA237cc787…，脚本43936f46…已设备哈希核对，owner仍0d7dcc2d…。

SSH runner收尾connection reset，随后只读连接取得上述完整最终结果，XML_REMOVED、boot3944/PID1978/2246保持；collector随后显式kill。证据取回目标out/video-decode/long-play，源/data/local/tmp/sq2-player-2160p-long-39441275-d887-4e17-a774-9e1fad93cdd3及sq2-long-kernel.log。下一步自动选择和真实应用兼容、能力广告/profile核验，再更新实验启动包/重启持久性；当前正常启动仍未启用新硬解，目标不可标完成。

**4K seek/flush通过（2026-10-29）**：组件新增独立configured_配置副本，flush关闭硬件会话后恢复SPS/PPS到待发送缓存；正常stop/reset仍清空。新的连续CSD序列替换旧配置，配置错误清两份。AIDL与独立组件构建通过。普通APK先显示60帧，再MediaCodec.flush+Extractor seekTo(5秒,PREVIOUS_SYNC)，实际同步点4.933333秒；输入215（含flush丢弃3），输出/渲染212，EOS/STOP/RELEASE正常，7000ms/realtime/pass=true，跳转后拒绝小于同步点的旧PTS。首次播放器错误要求精确5秒，已纠正，不能记为驱动失败。证据out/video-decode/seek/sq2-player-2160p-seek2-39441275-d887-4e17-a774-9e1fad93cdd3。当前owner0d7dcc2df26dbe3ef2dcd4db68aa419a460d745102c3f22f88a6d9025d94004d，private wrapper/mapper保持。

**4K通过轮收尾已核对**：完整结果、两张截图及kernel log已取回out/video-decode/import-retain/；晚期截图可见测试图样，非4K逐像素证明。collector实读KMSG_END reason=timeout/120000ms，已结束。boot3944、system_server1978、SF2246、guardY保持，XML_REMOVED，全局minigbm仍c1a57082…。服务继续以私有候选待命；此覆盖下方收尾尚待核对说明。

**4K30首次实时通过（2026-10-29，最新）**：组件新增有界独立AHardwareBuffer CLONE导入引用（最多8个/128MiB/单FD16MiB，按dev+inode去重），只在私有映射缓存开启时使用。保留分配对象而不持有C2输出block/队列槽位、不持续CPU lock；stop清理pool后释放所有import。配合上一轮私有minigbm缓存，使缓存映射能跨每帧C2导入对象销毁存活。RAM实际owner SHA95f3f6122c58d671064330c10e5baa3738702e51a3dce13223cb85c7ff627da1，wrapper仍bdf11d60…，私有库b6734690…。

先Surface独立CSD六帧全字节匹配FFmpeg（out/video-decode/import-retain/six），再普通UID APK 3840x2160/30fps/10秒300帧：inputs/outputs/rendered=300、EOS、realtime=true、pass=true，9962ms，STOP_OK/RELEASE_OK/RESTORED。copy+sync由16.909s降至1.129s（Y0.540s、UV0.265s、DMA0.323s），fetch5.088s含输出池等待；4个唯一输出buffer，停止记录IMPORT_RELEASE count4/50184192B。证明本测试4K30实时通过，仍不是长期稳定/seek/flush/高profile/HEVC/VP9/B站自动选择或默认启动完成。上限/错误路径与实际释放长期检查仍需完善。

当前服务PID8362私有namespace，native驻留，不重载PAS。日志/data/local/tmp/sq2-player-2160p-import-39441275-d887-4e17-a774-9e1fad93cdd3/及sq2-import-kernel.log；收尾kill/check与SCP正在遭遇间歇连接超时，需核对本地取回与collector有界结束，不能据发出命令记为已完成。启动镜像仍旧实验service，全局图形库未主动修改。下一步先归档/核验画面与系统状态，再长播放/seek/flush、正常应用选择、持久化包；不要因4K单轮通过标记全目标完成。

**映射缓存真实4K已完成但未提速（覆盖下方尚未启动状态）**：网络恢复后chmod755 owner并启动成功。私有服务PID23569实际/root/vendor/lib64/libminigbm_gralloc.so=b6734690…，环境SQ2_VIDEO_MAP_CACHE=1；Android init全局库仍c1a57082…。4K300帧/渲染/EOS/STOP/RELEASE完成，20971ms，copy+sync16.909s，仍pass=false/realtime=false；OUTPUT_UNIQUE=3。collector已kill，runner返回RESTORED。不能把独立buffer的缓存收益当作实际播放性能修复。下一步观察每帧C2GraphicAllocation/mapper导入对象是否被销毁，导致BO最终释放清掉缓存；必要时仅保留独立import引用来维持映射对象生命周期，不能持有BufferQueue输出block阻止归还，也不能持续CPU lock绕过消费者同步。该销毁解释目前仍是待验证假设。

服务目前运行私有wrapper→owner，默认镜像未改，native/PAS不重载。日志设备/data/local/tmp/sq2-player-2160p-map-cache-39441275-d887-4e17-a774-9e1fad93cdd3/与sq2-map-cache-{private,off,kernel}.log，取回目标out/video-decode/map-cache-results。后续核对实际文件，不把网络不稳定下的SCP命令发出等同于归档完成。

**映射缓存候选私有A/B通过，真实4K尚未启动（2026-10-29）**：新增prepare/build-sq2-minigbm-map-cache，固定minigbm455b745…/drv.c6655bfd5…与donor1ab63ce8…。候选C桥b673469018e0dcc7eb3ba238f2e36681a3265c2399db2682b958b75f2e10d210，默认关闭，SQ2_VIDEO_MAP_CACHE=1仅实验进程启用。仅MSM/无bo_flush/非cached/非protected/线性NV12/RW/单映射≤16MiB；每driver最多16缓存项、128MiB，附加一个mapping引用，普通unmap继续减活动引用，BO最终destroy仍释放。WC发布前dsb sy，原mapper fence/锁解锁不绕过。helper与实际源抽取unmap/destroy的ASan/UBSan测试覆盖默认关闭/拒绝条件/10000次复用/预算满拒绝/18次最终回收，通过；不代表所有并发或导入格式同步认证。

LD_PRELOAD试验因sphal命名空间缺cros_gralloc_driver::retain符号而只使独立probe abort，未改系统库；已改用sq2-map-cache-private.c新mount namespace+根递归private+单库bind，只有该子进程看候选。memory-probe开启时两次重新map首写858/865us、0minor faults，所有字节正确；关闭时35521/35555us、2025faults。系统库仍c1a57082…，框架PID1978/2246保持。日志在设备/data/local/tmp/sq2-map-cache-{private,off}.log，尚未全取回。原owner/remap取回仍因间歇网络失败，不误记已归档。

准备真实4K：RAM /vendor/bin/sq2-c2-experimental已换私有启动器SHA bdf11d60a20e2433dbb8c5fb84e06a15b02806785979dcb0f802aae4d5408183；--serve应exec/data/local/tmp/sq2-c2-owner（b113c5d4…）并设置缓存。**最后观察init服务stopped，初始/data临时owner文件缺执行位；后续chmod/start命令SSH均超时，不能认定已执行。** 下步先连接，chmod755该已核验owner，启动并核对PID/exe及进程私有库哈希/namespace，再启动独立collector验证ready/smoke，运行test-c2-player.sh 2160p-map-cache。该label测试还未启动，sq2-map-cache-kernel collector命令也连接失败，不复用或猜日志存在；先核对。不要重载native/PAS。本boot仍3944，默认镜像未改；系统全局库始终原版。若缓存服务无法启动，RAM恢复/vendor/bin为/data/local/tmp/sq2-c2-owner即可回原候选。

**本轮收尾网络限制**：上述remap及owner结果已从SSH stdout读到，但最后SCP归档全部连接超时；out/video-decode/output-mapping尚无完整原始文件，不能按已取回处理。源仍在设备/data/local/tmp/sq2-c2-memory-remap.log、sq2-owner-kernel.log、sq2-player-2160p-owner-39441275-d887-4e17-a774-9e1fad93cdd3/。SCP/SSH进程均已退出，无后台传输；最后清理/boot/PID检查在失联之前完成。先恢复连接并取证，不据超时推断整机故障；此前用户确认正常不能替代当前屏幕观察。

**映射反复缺页与输出三缓冲复用已实测（2026-10-29，最新）**：memory-probe增加getrusage及同一graphic allocation两次unmap/map；8.29MB首次写40326us/2025 minor faults，每次重映射后35537–35807us/2025 faults，映射内后续约0.7ms/0 faults。真实C2新增前12帧pool/allocator/inode及总unique统计：pool17/allocator20，三个inode9871/9873/9875循环，300帧OUTPUT_UNIQUE=3，说明输出pool已实际复用。4K300帧20952ms，渲染/EOS/STOP/RELEASE通过但实时性失败；copy+sync16.847s。主要问题是重复映射/缺页，不再按“每帧新分配输出”解释。

恢复固定minigbm源码455b7456397dff01443584b09e2fd594079e905c至/root/.cache/sq2-video-minigbm-reference。drv_bo_flush_or_unmap在backend无bo_flush时调用unmap；MSM只有bo_map/bo_unmap，cros_gralloc unlock每次走此路径；drv_bo_mapping_destroy在BO最终释放时清理所有映射。kernel msm_gem_fault逐页vmf_insert_pfn，与测量一致。下一步构建仅实验解码进程启用的映射保留候选，必须保留cache/fence同步、限制映射生命周期/预算、验证最终释放，不能简单常驻C2GraphicView或盲目跳过unlock。尚未修改/替换minigbm或kernel。原minigbm构建输入目录已缺失，不可盲跑旧build-sq2-minigbm-bridge.sh覆盖现有库。

当前RAM服务b113c5d4dd129f4f36c8d5a9b7eb81609a85ff99ccf9c49223681c65fb177ab9；证据out/video-decode/output-mapping，collector停止，XML撤销，boot3944/PID1978/2246保持。默认启动镜像未改。

**连接恢复、首次gralloc写入慢已测得（覆盖下方失联状态）**：用户确认屏幕正常，SSH恢复；boot3944/PID1978/2246未变，guardY。逐轮allocation-only probe已运行并全字节检查：8.29MB cached→gralloc首次41207us，后续717–793us；system heap→cached约530us；gralloc→cached每轮约5.8ms。首次访问慢是实测，具体是否page-fault/缓存/映射开销尚未核实。不能把临时网络超时当作设备挂死。collector已结束，XML已撤销。日志取回out/video-decode/output-pool，pool轮累计copy+sync16.929s（Y11.137s、UV5.551s、sync0.240s）。仍未证明实际输出buffer复用，下一步记录pool local ID/allocator及每帧allocation身份，检查反复map/unmap是否重建PTE，再选择修复。pool-interface-test实机6参数及配置1234/query回读通过，尺寸合法/非法边界仍通过。RAM候选仍38267973…，未写启动镜像。

**输出池参数候选与测试后失联（2026-10-29，当前先取证）**：新增allocation-only sq2-c2-memory-probe.cpp，不打开video/PAS。8轮8.29MB复制全字节正确：cached↔cached/system heap约4.3–4.6ms总计，cached→gralloc45.5ms、gralloc→cached46.8ms、随后heap→gralloc6.0ms；提示首次映射访问可能昂贵，但逐轮计时修订只编译，未跑，不能定论。新增C2PortBlockPoolsTuning::output参数，使框架可配置输出pool ID；AIDL构建通过，pool参数往返fixture仅编译待运行。

RAM候选382679735c529b041a07391f8ee4963bf99571a4b8f141ea46ba0fe09e358aad实测4K：300输入/输出/渲染，EOS、STOP_OK、RELEASE_OK、RESTORED均返回，21034ms，仍pass=false/realtime=false。不能据约1.2秒差异认定pool已实际复用；需取回logcat核对pool ID/类型及累计复制时间。测试随后SSH连续超时，用户已被询问屏幕/IP，尚无当前设备状态回答；不能判定整机挂死或自动重启。最后已知boot3944。本轮collector在/proc/1/root/data/local/tmp/sq2-pool-kernel.log，120秒/2MiB有界；显式kill连接失败，待核对终止状态。日志目录/data/local/tmp/sq2-player-2160p-pool-39441275-d887-4e17-a774-9e1fad93cdd3，尚未取回。逐轮memory-probe传输失败，旧设备binary不代表新源码。恢复后先读boot/上述日志/pstore，不重载固件，不盲目重跑。默认启动镜像未更改。

**复制微优化未提升4K（2026-10-29，最新）**：分离DMA同步/亮度/色度计时，并加入有边界的ARM NEON行复制/UV拆分/UV交换helper。ASan/UBSan主机与静态ARM64 QEMU遍历长度0..2050、16种源对齐、尾部及目标哨兵通过，AIDL构建通过。实机布局NV12（cols1/2/2，U/V指针差1，root1/1，offset0/1），原代码已走memcpy；平面及VU优化未命中，不能当作性能改善。显式向量行复制同样未改善：4K300帧22253ms，pass=false/realtime=false，EOS/STOP/RELEASE通过。累计copy+sync18.166s，其中DMA同步0.242s、Y12.347s、UV5.576s；下一步分别测量源读/目标写与内存映射属性，不再猜测色度转换或VPU时钟。

当前RAM服务SHA7d178dfa1b5e62604e0b25a148f9c86898b071fb4a96101adb14342ceac62902。此版本Surface独立CSD六帧再次逐字节匹配参考；不等于4K逐像素验证。证据out/video-decode/fixed-capture/sq2-player-2160p-{neon,vu,vector}-39441275-d887-4e17-a774-9e1fad93cdd3及vector-six。boot3944/PID1978/2246/guardY保持，XML撤销、有限collector停止，启动镜像未改。helper目前保留为已验证但无性能收益候选，不标为4K修复。

**4K耗时定位（2026-10-29）**：计时候选ae0f01160a605fa8138a4504bc4e5ae98d2df940e71d88efee545e37bb5f53dc已替换RAM服务。新APK实测300帧22230ms，正确判定pass=false/realtime=false，EOS/STOP/RELEASE通过。累计fetch0.283s/map0.090s/复制含DMA同步18.176s/unmap0.034s，优先分离同步与复制并优化像素搬运；没有证据需提高VPU时钟。证据out/video-decode/fixed-capture/2160p-timing，XML已撤销/collector停止/boot与框架PID保持。本轮用户确认现在可操作，旧黑屏反馈不是新故障。最新候选仍仅RAM部署，未写启动镜像。

**固定capture池修复1080p释放；4K尚不实时（2026-10-29，覆盖下方旧状态）**：video fd close的内核栈定位到drm_gem_put_pages/dma_buf_release/__fput。旧组件每帧更换gralloc capture，与驱动必须保留映射直到session END的安全策略叠加，造成分配/回收随帧数增长。保留驱动退休隔离及预算，组件改为固定dma-heap capture池，DMA同步后复制到独立C2输出；这是硬件解码加一次CPU像素复制，不是零拷贝。主机原替换模式及复用100帧仅5次分配的ASan/UBSan通过，完整AIDL构建通过；实机Surface独立CSD六帧与参考逐字节一致。

RAM候选89ab6dc6272eb94d435fe5a3f3f240409ab1d0e1c460ffe7aa5d97d0244297df：1080p30/300帧全部输出及渲染、EOS/STOP/RELEASE通过，耗时9963ms，固定9个3MiB capture；fd close约7ms。4K3840x2160/300帧全部输出/渲染和释放通过，但10秒视频耗时22295ms（约13.5fps），**不是4K30通过**；旧日志pass=true只判断完整性，播放器源码已补实时性判定，尚待新APK实测。证据out/video-decode/fixed-capture/{six,1080p,2160p}。新增输出fetch/map/copy+sync/unmap累计耗时诊断仅编译，尚未部署。

用户澄清“卡住或黑屏”为之前SPS/PPS失联，当前可操作；只读核对boot39441275-d887-4e17-a774-9e1fad93cdd3、PID1978/2246、guardY、Android完成1保持。当前native驻留且本boot已PAS一次，禁止重载。临时media XML已撤销。实验启动镜像仍旧服务，RAM修复未持久化；默认启动保持正常闭源。最新fetch远端仍c19ba9b，集成修改在工作区。下一步测量4K分配/映射/复制耗时并完善输出pool，不能据完整帧数宣称实时性、B站兼容或全目标完成。

**720p通过；1080p关闭超时定位到video fd close（2026-10-29，最新）**：新增只读sq2-v4l2-capabilities.c实查H264尺寸96..4096两轴、step1，profile列表Baseline/Constrained Baseline/Main/High/Stereo/Multiview（仅报告非逐项验证）。AIDL接口实验范围改96..4096 step2；临时XML按每轮尺寸上限广告，仓库默认XML仍320x240。普通APK 1280x720/30fps/10秒300帧完整播放、300渲染回调/EOS/release0通过，boot3944/PID1978/2246保持。

1080p首轮300帧及300渲染/EOS通过但stop报错，后续诊断两轮为299渲染/300输出，均STOP_ERROR code=-2147483648、release仍执行，不能算完整通过。真实C2 onStop stateEnded/pending0，STREAMOFF两队列+输入unmap约3ms，**close(video fd)耗时3.69秒**，capture leases释放约4ms；整个session close最终返回0，但框架3.3秒先报UNKNOWN_ERROR。当前候选SHA24cc24083e466a27ba28efa4fc991234c9d8f13796bd7be195654e4361fe7cdd在RAM vendor驻留，包含close-stage Android日志；内核未改，无PAS重启。证据out/video-decode/player-app-1920x1080/close-trace及720p/hardware。所有临时XML已清理、测试应用已停止、collector结束，系统PID保持。下一步针对fd close采样内核栈/分析vdec_close及vb2释放，修复耗时后再继续4K；不要只调高超时或把1080p画面出现当作完整成功。当前未发现可用/sys/kernel/{debug/,}tracing/available_tracers路径。

**普通APK可见硬件播放300帧通过（2026-10-29，最新）**：新增probe/video/player-app、build-sq2-video-app.sh和test-c2-player.sh。无privileged权限的Java MediaExtractor/MediaCodec/SurfaceView应用（当前user10，UID1010254）播放FFmpeg生成320x240/30fps/10秒/300帧AVC baseline MP4，显式选择实验codec。软件与硬件采用同一按PTS限提前10ms提交的播放器均inputs300/outputs300/rendered300/EOS/pass=true、release成功；硬件耗时约9964ms，两张截图含变化中的测试图样及烧入时间/帧号。证据out/video-decode/player-app/hardware-paced。当前boot3944/PID1978/2246保持、guardY，临时XML已清理/缓存刷新，应用force-stop撤销KEEP_SCREEN_ON，native仍驻留。

保留限制：最初不限制render-ahead时，软件自然节流而硬件约1秒即解出300帧，只59次渲染回调、屏幕提前到最后帧。新增应用PTS节流使正常可见播放通过，但不是组件buffer-pool/backpressure已修复；需继续补接口/输出pool等参数，不能靠测试播放器证明所有第三方应用兼容。当前XML只广告320x240，接口fixture最大640x480，不代表硬件极限。下一步基于真实V4L2能力补参数、验证更高分辨率及长播放/seek/flush，然后接B站与4K，仍未完成全目标或持久化默认启动。

**独立CSD硬解与Surface/ImageReader像素均通过（2026-10-29，最新）**：经核验BC711序列号/ESP B027-0512/marker/mapper root，使用既有一次性env启动实验镜像a5038428…，boot39441275-d887-4e17-a774-9e1fad93cdd3，guardY、Android1、PID1978/2246，一次性env已消耗0。停止空闲实验服务，在RAM vendor原子替换为CSD候选SHA8382c98d4fb92db3f892b91419bc160a37a4d90874fb42837dcf3a3128a84ae0后启动；持久镜像仍旧服务。独立collector ready/smoke后本boot唯一一次video-decoder-lab-start成功，native驻留，不再PAS或热卸。

先--csd-stripped真实硬件六帧/EOS/全部像素通过；再新增--surface-csd，AMediaCodec配置AImageReader的ANativeWindow，releaseOutputBuffer(render=true)，消费实际三平面图像，按row/pixel stride打包I420。软件对照及硬件各六帧全字节匹配FFmpeg参考，Image timestamp_ns为0/33333000/66666000/99999000/133332000/166665000，尺寸320x240，crop全幅，EOS正常。证据out/video-decode/ndk-vector/{hardware-csd-stripped,software-surface,hardware-surface}。两硬件轮collector有界并正常signal结束，media XML已删除且缓存刷新，boot/PID不变；Surface轮内核日志无检出的fault/panic/HFI错误。仍不是可见SurfaceView/播放器/长视频/4K证明，不宣称零拷贝或旧黑屏根因已修复。当前实验服务新候选与native驻留；默认下次启动仍正常闭源。下一步普通APK可见Surface播放、完善能力参数和真实分辨率/长视频测试，再持久化新候选。

**AIDL修订候选已构建；去除in-band配置的软件对照通过（2026-10-29）**：恢复固定hardware/interfaces/native源码、生成144个AIDL头，包含CSD缓存修订的AIDL服务编译成功。设备正常boot1486上不带--serve执行候选：STORE_STATUS0、LOCAL_LIST一项c2.sq2.avc.experimental/video/avc、exit0；不注册、不替换服务、不启硬件。NDK帧探针新增--csd-stripped，将28B SPS和9B PPS仅置于csd-0/1，从AU中剔除，首AU5039变5002。c2.android.avc.decoder实际六帧/EOS通过、独立FFmpeg参考全字节一致，证据out/video-decode/ndk-vector/software-csd-stripped。旧--csd保留in-band的行为仍保留作不同试验，不要混称。当前仍正常启动、无video0，PID2058/2350未变。下一步在实验启动中使用新AIDL候选（out/video-decode/codec2-integration/aidl-store/interface-test），对--csd-stripped做有界且持久日志的真实硬件测试；旧boot bundle仍含旧服务，不能误以为新二进制已安装。Surface/应用/高分辨率仍未验证。

**设备恢复、配置work实机完成（2026-10-29，覆盖下方受阻状态）**：SSH恢复，boot1486cdb1-6666-45df-a94b-6b1d5d9189c2，Android1/guardY/nohibernate，正常闭源启动项无experimental参数；system_server2058/SF2350。实验服务/XML/video0均不存在。CSD日志确为0字节、pstore空，当前tombstone为恢复后Trusty/gscd/init子进程噪声，没有旧测试阶段证据；不能确定旧CSD探针执行到送帧或认定其为卡死根因。取回实际库并通过原固定哈希，恢复平台头后包含CSD修订的完整组件测试程序已编译。新增--config-only在无/dev/video0的正常系统运行：两项配置work callback result0/processed1、无图像/EOS、stop/release0、exit0；boot/PID不变。该模式使用不透明配置字节，只验证C2配置缓存/回调所有权，不验证SPS/PPS语法或硬件。证据out/video-decode/codec2-integration/component-backend/config-only.log。尚未替换AIDL服务或镜像，下一步恢复AIDL构建依赖，再测试修订候选配置+真实帧及Surface路径；不盲重跑旧CSD实验。

**用户确认设备卡住或黑屏（2026-10-29，最新状态）**：用户回应独立CSD试验失联询问，明确报告“设备卡住或黑屏”；不再仅视为SSH网络故障，但尚无内核/固件根因证据。已请用户强制关机后选择默认闭源Vulkan正常启动项恢复，未收到恢复确认；随后只读SSH检查仍超时。恢复后先读取持久/data/local/tmp/sq2-hardware-csd.log、pstore、boot ID及服务状态，不运行旧CSD测试、不自动加载后端。人工恢复重启不能记为自主崩溃。CSD缓存修订仅主机helper测试通过，完整组件尚未编译/部署；构建脚本实际因缺设备匹配libcodec2_soft_common.so而停止，不能拿Quest2的同名库顶替。

**MediaCodec硬件六帧像素通过；独立CSD试验失联待取证（2026-10-29）**：已fetch origin，当前codex/sq2-video-decode与远端c19ba9b一致，后续集成改动在工作区。早期VINTF/init实验服务已在候选启动生效，系统Binder的default1可发现；临时media_codecs_c2.xml加media服务刷新解决列表准入。独立NDK探针必须启动ABinderProcess_startThreadPool，否则软件对照也会卡在输入队列。修正后软件六帧及硬件c2.sq2.avc.experimental六帧均输出320x240 I420、正确PTS和EOS；转换NV12后与FFmpeg独立参考逐字节一致，十二CRC与原native记录一致。硬件成功日志和六帧保存在out/video-decode/ndk-vector/hardware-output，校验脚本scripts/verify-sq2-mediacodec-frames.py。成功轮boot434055bb，实验退出0、软件创建对照0、RESTORED；不等于Surface/播放器/4K支持。

**当前需先恢复连接取证**：随后test-c2-media-refresh.sh --frames --csd（独立csd-0/1）运行期间SSH reset，重连超时且ping不可达。日志写在设备/data/local/tmp/sq2-hardware-csd.log，尚未取回，不能推断板级重启或解码失败根因；XML回退也尚未复核。最后成功观测system_server2001/SF27412、guardY、native core驻留、init服务running。恢复后先读boot ID/该日志/进程/XML，按实际状态恢复临时XML，禁止重新PAS或热卸provider。用户已被询问设备屏幕/网络状态。原默认启动镜像未提升；当前候选native-c2-experimental的一次性env已消耗，下次正常启动回原闭源入口。独立CSD、Surface、完整参数/异步输出/长视频/高分辨率仍待完成。

**AIDL实验注册/跨进程发现通过，MediaCodec发现未完成（2026-10-29）**：新增sq2-c2-aidl-service.cpp实现C2ComponentStore并使用设备libcodec2_aidl_noisurface.so包装；新增sq2-c2-aidl-client-test.cpp独立Binder客户端。固定hardware/interfaces 1a56e38edc2f2f6189ef405ee1edce554e15cbc0、frameworks/native 4f463a6b1de9198963dc6aff74154a504ba3f8f6，冻结C2 v1接口与bufferpool/common等生成NDK头；实际设备库报告AIDL v2/hash2f7448ba8211c5cd1de7bd6c17fe7c5fd034ebbe。首轮未传--hash导致生成头缺getInterfaceHash虚函数，asBinder错位至getInterfaceHash、独立进程139；添加冻结版本/hash后修复，非板级崩溃。wrapper sizeof352与设备QTI provider分配大小一致，仅局部ABI证据。正常default1注册返回-3/exit5；该实例未VINTF声明。显式--serve-local降为本机稳定性后临时default1注册0，独立客户端跨进程listComponents返回c2.sq2.avc.experimental/video/avc，createInterface/getConfigurable非空、client exit0。**这只是实验AIDL发现，不是MediaCodec发现完成：同轮AMediaCodec目标创建仍NULL/exit2，软件对照成功；框架按VINTF声明发现store。** 12秒有界服务正常timeout124后已核实SERVICE_ABSENT、仅原default/software保留、PID1968/2172不变。没有改VINTF/XML/默认服务/启动盘/固件。下一步实施可回滚VINTF实例声明与服务发现刷新，再补必要组件参数并测试MediaCodec创建/解码/Surface；不要把--serve-local作为正式集成或安全策略配置。证据out/video-decode/codec2-integration/aidl-store/local-live.txt。用户要求保存并推送；本轮保存源码/构建准备脚本和状态，raw logs/二进制/密钥留out/private。

**独立C2组件真实帧回调通过（2026-10-29，本轮提交记录）**：新增sq2-c2-v4l2-component.h将process读取C2Work linear block→通用V4L2 session→C2BlockPool graphic block→C2Buffer回调连通；后端不含向量、不fork旧runner。测试向量仅在sq2-c2-component-test.cpp。a101同boot实测六输入C2Work+单独EOS，七回调result0、六帧全部12CRC匹配、ordinal校验通过、mask3f/eos1/errors0，stop/release0，客户端保留图形buffer到组件关闭后再释放；SSH0/PID1968/2172不变、guardY/pstore空。首轮collector smoke前缀被过滤，脚本exit1且未启动组件；改白名单前缀与新v2日志后通过。补充四角色BASELINE/ROLLBACK旧队列探针返回OMITTED38、MODIFIED真实解码，均程序exit0；工作负载不同，不称三次硬件解码。证据out/video-decode/codec2-integration/component-backend。**仍非MediaCodec可用服务：AVC单路径、接口五参数/fixture范围、未AIDLv2注册、未Surface显示；动态分辨率变化拒绝，drain()仍OMITTED，输出pump依赖process而无独立事件线程，codec-config/partial-frame/reorder/异常/重启矩阵待补。下一步补组件生命周期与异步输出/参数能力，并接AIDLv2独立store与MediaCodec端到端；不要用通过向量推断其它codec能力。** 用户要求完成本轮后提交，保持private/out/密钥/原始日志不入库；没有改启动盘或系统媒体服务。

**V4L2通用会话后端候选（2026-10-29）**：新增probe/video/sq2-v4l2-session.h，不含向量/CRC/固件装载，open(codec,width,height,allocate,deliver)、submit实际字节与opaque token、pump、requestDrain、close。输入MMAP槽，输出由外部分配DMABUF owner，DQ交付旧lease后申请新lease才requeue，避免覆盖客户端帧；SOURCE_CHANGE协商真实NV12几何/最小buffer数，EOS待输入归还后STOP/LAST，错误sticky，close先streamoff/close再释放owner。动态分辨率变化暂明确ENOTSUP，不伪报支持。tests/sq2_v4l2_session_test.cpp实际头+wrapped syscalls交叉编译/QEMU通过：任意5字节输入、token123456789、替换lease、EOS、替换ENOMEM保留客户端owner、close后客户端释放，均非硬件。NDK35 -Wall/-Wextra/-Werror编译通过；首次g++不存在exit127已改用aarch64-linux-gnu-g++。旧固定runner未改。补充四角色以新头文件空基线表示API不存在，modified mock通过，rollback恢复空头；不要混称设备回滚或三次硬件试验。证据out/video-decode/codec2-integration/session-backend。**下一步：实现C2组件allocate回调从C2BlockPool fetchGraphicBlock/map检查真实stride/offset/容量，deliver用C2Buffer及work ordinal完成pending work；process读取C2Work输入linear block提交，不再fork固定向量runner。先补mock失败边界/编译，再有界实机组件解码验证。** 当前QueueProbe仍OMITTED，尚无可注册组件或MediaCodec证明。

**真实C2工作队列基础类通过（2026-10-29）**：接口类分离到probe/video/sq2-c2-interface.h，原接口测试保持；新增sq2-c2-queue-test.cpp继承设备实际libcodec2_soft_common的SimpleC2Component，独立进程start→queue_nb移交一项C2Work→worker回调→stop/reset/release实际通过exit0。process明确返回C2_OMITTED38且不输出帧，**没有接解码后端，不是可用解码器**。使用system优先、swcodec APEX其次的进程局部LD_LIBRARY_PATH，不改全局或服务。基础头实际位于同固定av提交media/module/foundation/include，修正初次找错路径编译exit1。四角色自包含源码BASELINE/ROLLBACK接口测试、MODIFIED队列测试全部实际exit0，patch重建/rollback hash一致；a101/guardY/PID1968/2172不变。证据out/video-decode/codec2-integration/component-queue。**下一步必须将V4L2后端从固定向量runner重构为接受C2Work实际输入的会话，输出由C2BlockPool提供并以C2Buffer/ordinal回传；不能把OMITTED占位或固定六帧子进程包装当作组件完成。** 同时补齐MIME/profile/allocator/pool参数和实查能力，之后AIDLv2独立store及MediaCodec端到端。当前未注册、未启动固件、未重跑小向量。

**C2组件参数接口初步实测（2026-10-29）**：新增sq2-c2-interface-test.cpp，真实链接设备libcodec2_vndk/libcodec2和平台libc++，SimpleC2Interface+自定义C2InterfaceHelper建立kind/domain/input LINEAR/output GRAPHIC/picture-size五参数，未注册服务。独立fixture范围320..640/240..480不是设备能力或最终范围。发现官方validatePossible仍TODO返回OK；初版接受641并assert退出134。改supportsAtAll/保留旧值后又发现C2R::plus不合并status（源码TODO mStatus），故OK.plus(BadValue)仍ret0但failure1；现用第一项BadValue初始化result，后续只追加失败。实测合法640x480成功；641返回22/failure1、查询仍640x480、exit0；baseline/rollback同输入重现assert134，补丁重建/回滚hash一致。初次RTTI链接失败改-fno-rtti，未改系统库。a101boot/guardY/PID1968/2172不变，未碰固件或服务。证据out/video-decode/codec2-integration/component-interface。**下一步：将接口类分离供组件复用，补齐真实能力来源及必要MIME/profile/level/allocator/pool参数，实现C2Component状态/工作队列/回调接入已有V4L2/gralloc后端；再独立组件测试与AIDLv2注册/MediaCodec端到端。** 不把五参数测试称为完整组件，不把fixture范围写进生产XML。

**C2 gralloc→V4L2真实帧通过（2026-10-29）**：a101同boot，图形布局探针实测320x240及320x256均159744B，row384，UV偏移98304/98305，前两fd同inode/dev，另metadata fd12288B；288高容量180224、UV110592，不能通用硬编码。新增sq2-c2-v4l2-bridge.cpp为当前向量保留九个真实DEFAULT_GRAPHIC对象，unwrap公开API后校验布局/容量/同buffer引用，dup到200..208给独立静态runner；runner --graphic-fds仅当前9buffer/159744B/384stride/256scanline，输入仍systemheap。**一次真实六帧全部12CRC匹配、EOS无ERROR、双STREAMOFF/close0、父进程C2_RELEASE_DONE child_status0、BRIDGE_EXIT/SSH0**。boot/PID1968/2172不变，guardY/pstore空，无固件重启或服务替换。完整证据out/video-decode/codec2-integration/graphic-layout/native-result.json及持久kmsg/stdout。布局BASELINE/ROLLBACK单240、MODIFIED三尺寸metadata均实际exit0；非三次解码。旧mock通过，新继承fd分支仅正向实机覆盖，失败注入待补。**下一步：实现真实C2Component/C2ComponentInterface配置/工作队列/输出block与回调，接实际AIDLv2 ComponentStore（libcodec2_aidl_noisurface），独立测试组件后再MediaCodec注册/解码/Surface显示与逐codec能力矩阵；不要继续用小向量重复替代组件接入。** 当前桥接仅实验进程，不是MediaCodec可用服务；保留系统软件回退，不假报XML能力。

**Codec2分配器真实调用通过（2026-10-29）**：独立sq2-c2-allocator-test.cpp用固定AOSP C2头、NDK r28c libc++头私有__1命名空间副本及设备实际libc++.so构建（gralloc枚举来自Mesa26.2.4 android_stub，无链接Mesa allocator）。a101同boot通过dlopen实际libcodec2_vndk、GetCodec2PlatformAllocatorStore、DEFAULT_LINEAR 4096B分配/map/写读368640/unmap/release；扩展DEFAULT_GRAPHIC format0x23 320x240分配/map三平面成功，row384 col1/2/2 depth8，unmap/release0。BASELINE线性、MODIFIED线性+图形、ROLLBACK恢复线性均实际exit0；patch重建/回滚hash一致，guardY/PID1968/2172未变。不改服务/固件/启动盘。仅局部ABI/分配器证据，不是完整ABI或MediaCodec解码证明。证据out/video-decode/codec2-integration/allocator-transaction。**下一步：检查C2 gralloc native handle及平面offset/容量，与V4L2 CAPTURE所需stride384/scanline256/size159744对接，验证真实解码帧写入Android图形buffer；然后构建AIDLv2组件服务与MediaCodec端到端测试。不能直接假定当前可见240高分配有256行容量，不修改已有媒体服务或假报codec能力。**

**原生DMA-BUF数据路径通过（2026-10-29）**：在a101c096-7d7e-4464-94c2-28d70b521b94同boot驻留固件上，runner新增--dmabuf：两队列使用system dma_heap进口fd、输入WRITE/输出READ显式CPU同步、失败逐项释放fd/mapping。实际六帧H264 320x240全部12CRC匹配、输入归还6、EOS空LAST flags0x104000无ERROR、两队列STREAMOFF及close完成，decode/SSH exit0；PID1968/2172与boot不变、guardY、pstore空、IRQ161。未重启固件、未改启动镜像。host ASan覆盖六fd/sync/close及第三次分配失败退回前两项；补充四角色patch重建/回滚哈希一致，BASELINE/MODIFIED/ROLLBACK同CRC自检均exit0，DMA-BUF源码分支False/True/False，非三次硬件对照。证据out/video-decode/venus-transport/dmabuf-frames/native-result.json及VERIFICATION.txt。Android provider实际PID1947 media.hwcodec，对应vendor.qti.media.c2@1.0-service，链接AIDL C2 V2与libcodec2_aidl_noisurface.so；另一个libcodec2_aidl.so链接V1，不能误用。官方v4l2_codec2固定af9fcc0819b689786af44c2711320414b29b01a3；frameworks/av固定e2f098935447ca4945946de5cb69db843fe3f003，缓存/root/.cache/sq2-codec2-av-reference。**下一步：匹配实际平台C++/C2 ABI，构建独立allocator/graphic-buffer接入验证，再接AIDL组件；保持现有媒体服务/软件回退。system heap成功不等于gralloc/AHardwareBuffer或Android MediaCodec接通，也不证明其它codec/长播放能力。** 不要重复小向量代替Android接入，不在本boot重新PAS或热卸provider。

**原生EOS修复及双会话实测通过（2026-10-29）**：preparer仅为SQ2把vdec_buf_done中空LAST从VB2_ERROR改正常DONE，非LAST空帧/legacy保持原样，后续HFI DATACORRUPT/DROP仍ERROR。新增test_sq2_vdec_buf_done.py抽取实际函数，128组合+missing-owner，BASELINE/ROLLBACK空LAST ERROR、MODIFIED DONE，均程序exit0；patch重建/rollback hash一致。全旧mock/三模块编译/5生成回归通过，测试已接build脚本。正常重启后boot a101c096-7d7e-4464-94c2-28d70b521b94，guardY/Android完成，11模块live符号闭包0/传输hash匹配，仅首次PAS。**相同严格runner（未放宽ERROR检查）连续两次独立会话每次6帧/12CRC全匹配、输入归还6、EOS event及LAST flags0x104001无ERROR、两queue STREAMOFF0/close完成，round与SSH全exit0**。IRQ102、PID1968/2172与boot前后不变；启动镜像未修改，临时模块驻留，不再PAS/热卸provider。仅证明320x240 H264 baseline双会话，不代表长播放/其它codec/AndroidMediaCodec。证据out/video-decode/venus-transport/eos-last/native-result.json及ledger。转入Android接入：本机Android17/SDK37/CL3B.260622.273.R1，实际AIDL IComponentStore default/software、vendor manifest AIDLv2；已有QTI服务/库未改。已克隆官方v4l2_codec2到/root/.cache/sq2-v4l2-codec2-reference，固定af9fcc0819b689786af44c2711320414b29b01a3，其service实际为HIDL1.2，不能直接冒充AIDL替换。**下一动作：检查实际AIDL provider依赖与开放v4l2_codec2组件/allocator构建接口，准备匹配Codec2/AIDL构建输入和NV12/DMA-BUF桥接验证，再注册真实组件；不靠XML假声明能力。** 保留默认软件回退，native kernel基础暂不再重跑小向量。

**原生V4L2六帧CRC通过，EOS ERROR仍开放（2026-10-29）**：用户已授权继续数据路径，并更新目标明确允许直接操作Surface。bf6d同boot驻留core上，--queue-only实测6QBUF/close0；首次完整模式QBUF与STREAMON0，但runner误将DQEVENT的ENOENT当致命错误提前streamoff/close。固定内核v4l2-event.c确认空事件返回ENOENT，已修正两个event循环，实际main mock覆盖ENOENT/EAGAIN→SOURCE_CHANGE后清理。第二次收到source-change，runner多余CAPTURE S_FMT将crop240重置为aligned256；固定vdec_s_fmt确认，改为仅非NV12时S_FMT。第三次真正输出6帧320x240、stride384/scanline256，全部12个Y/UV CRC与独立ref匹配，输入归还6，STOP0，收到EOS event及空LAST缓冲flags0x104041（含ERROR）；严格runner仍exit1，未掩盖错误。两个STREAMOFF/close0；boot/PID2047/2213不变、guardY、pstore空，IRQ最终116，最后kmsg无invalid/fault/error筛选结果。原始ad8e失联根因仍不能由本次通过推定已修复。**下一动作：修复SQ2 vdec_buf_done的空LAST判错分支**（原代码所有!bytesused设VB2_ERROR），保留非LAST空帧及HFI DATACORRUPT/DROP的错误，实际函数测试/编译后验证正常EOS，再Android Codec2接入与多编码/尺寸能力矩阵。不能在bf6d重新PAS或热卸提供者；当前内核未做EOS修复，需新boot部署（用户已允许直接操作设备，仍核对状态/保护/保留启动盘）。六帧证据out/video-decode/venus-transport/qbuf-trace/native-result.json，runner SHA61ce8f49…，stdout SHA8e0c1966…；测试/角色源对照不等于三次硬件对照。

**原生初始化/申请阶段实测通过（2026-10-29）**：恢复后实核boot bf6d6bff-756c-457e-872c-dd0ddffbe711、guardY、Android完成、pstore空。已取回ad8e六帧日志，只有READY/smoke/audit，没有结束/驱动错误/帧标记，根因仍未知。用户随后明确允许“不送帧”的分阶段定位。preparer新增默认false/只读sq2_stage_trace，SQ2专用、atomic饱和128条，记录8处调用前后（PM、session init、input resolution、两类bufreq、queue counts/PM put），无额外MMIO/睡眠/重试；实际helper host ASan检查默认关闭/legacy零日志/1000调用只128条，5生成回归/全部旧mock/三模块编译通过。v1函数终点过宽导致anchor错误，v2收窄修正。runner新增--format-only、--buffers-only及配置ioctl前后fsync；实际main mock证明格式模式无REQBUFS，申请模式6 mmap，两者无QBUF/STREAMON、close一次。bf6d干净启动只启一次PAS，新overlay自动匹配decoder，无driver_override。真实format-only与buffers-only均exit0/close完成，6输入buffer共2949120B；16阶段日志ret0（SESSION_INIT、输入/输出需求均有回复），IRQ3→7，PID2047/2213与boot均未变，guardY、pstore空。**未送帧，不是ad8e挂起根因修复**。当前core/固件resident，不能再PAS启动/热卸provider。下一步审查并为QBUF、STREAMON/资源装载/首次输入/首个source-change建立有界定位，再获准推进数据路径；不要直接宣称V4L2帧/Codec2可用。下一阶段可复用本boot驻留core，不需重载固件。候选stage /root/.cache/sq2-venus-stage-trace-v2；证据out/video-decode/venus-transport/stage-trace，旧四角色保持，source trace False/True/False全exit0、patch重建/rollback hash一致；初次源码检查literal误带分号已修正并加入stdout断言，原错误记录保留。

**原生六帧首次执行中断（2026-10-29）**：新增probe/video/sq2-v4l2-decode-test.c，复用固定h264-vector，非阻塞stateful OUTPUT→SOURCE_CHANGE→NV12 CAPTURE，有限buffer/20秒循环、逐可见行CRC/唯一帧、输入EBD后STOP与LAST、streamoff/close；外层40秒timeout不保证解除内核卡死。ARM64静态编译/QEMU CRC已通过。在ad8e同boot现有/dev/video0执行，不重启PAS，独立120秒/2MiB collector smoke通过，随后SSH超时exit255，只拿到执行前IRQ3/PID1975/2140，没有runner完成输出。用户回复“正在重启”，不能写成自主崩溃。尚未恢复SSH（多次connection closed），**下一步首先恢复后只读取/data/local/tmp/sq2-native-frames-ad8e.kmsg及pstore/boot状态，不加载任何模块、不重复测试**；当前确切停止位置未知，不猜根因。首版stdout/stderr误放/tmp，重启可能丢失；已保存TESTED_SOURCE.c/TESTED_RUN.sh/实测binary，未来脚本改持久/data目录，runner新增每个配置ioctl前后日志，修改版仅编译未部署。实际CRC helper对独立ref.nv12六帧以384stride/256scanline填充检查12CRC并验证可见像素变异拒绝，ASan/UBSan通过，但不是硬件帧。补充四角色源trace False/True/False、同CRC自检cbf43926/exit0、补丁重建和回滚哈希一致，旧四角色保留。当前Android Codec2/原生帧和能力上限均未完成。证据out/video-decode/venus-transport/native-frames。

**首次原生IRQ/V4L2接入（2026-10-29）**：用户明确允许正常重启，setprop sys.powerctl reboot后boot由bcd0变ad8eb3bc-42af-47e6-a592-82726fa0e14c，guardY/boot_completed1。11模块当次导出missing0仍漏命名空间：实际venus-dec装载被DMA_BUF/dma_buf_put拒绝（PAS未启动），preparer新增MODULE_IMPORT_NS(DMA_BUF)，全mock/三模块编译与5生成测试通过，修订decoder实际装载成功。collector首轮180s/4MiB超上限，已在任何insmod前失败；修订120s/2MiB记录完整。原生parent首次PAS认证0并成功probe绑定，hfi_core_init等待SYS_INIT成功才可到此；实际GIC206/SPI174中断计数3。decoder子节点误写qcom,venus-decoder，原驱动匹配venus-decoder；本boot仅对确切子节点driver_override绑定，无固件重启，/dev/video0 QUERYCAP/ENUM_FMT exit0，输入MPG2/H264/VP80/VP90/HEVC、输出NV12/Q08C。overlay源码已修正并重编译SHA2c95b3ba…，未热换；下次freshboot用新overlay。system_server/SurfaceFlinger PID1975/2140前后不变，boot不变、pstore空、筛选未见SMMU fault/Oops。**尚无原生V4L2帧/CRC/EOS，更无Android Codec2**。当前固件已启动一次且保持resident，不能再PAS启动或热卸provider；下一步在本boot现有/dev/video0构建/运行有界stateful H264六帧runner（沿用已核验h264-vector及独立NV12参考），记录source-change/stride/bytesused/可见YUV CRC、EOS与close；失败保留并先取日志，不用私有HFI重复启动代替。若需重新boot须另取用户许可。证据out/video-decode/venus-transport/native-first-boot，原四角色保持并补充源对照False/True/False；它们不是三次硬件试验。

**stop/flush闭环与live预检（2026-10-29）**：实际生成stop_capture/output/streaming、hfi_session_flush及FLUSH reply抽取编译640组合，覆盖SQ2/legacy、close与streamoff、两队列/八状态/五种回复失败/原错误。确认vdec_flush_done仅日志、这些stop路径不等reconf；close抑制callback仍complete控制回复。复现stock stop_streaming清零session_error，SQ2现保留原错误并置flush/core错误，start_streaming错误入口拒绝重启；baseline/rollback timeout error0、modified1，全部程序exit0，ASan/UBSan及三模块/4回归通过。只读SSH实核bootbcd0、6.3.8-sq2-android1、guardY，旧overlay/VIDEOCC仍驻留。新core/dec单独live符号预检exit1（媒体依赖未载），加入foundation六媒体模块后八模块missing0、exit0；capture SHA8596a1ba…，full-preflight.json记录依赖，**非ABI/加载证明**。未启动固件/重启。已异步询问用户是否现在方便正常重启，未收到许可前不执行。下一动作：准备有界native V4L2 runner、收集器与八模块按依赖装载清单/哈希，核验overlay匹配及freshboot状态；获重启许可后正常重启，干净boot首次只跑原生IRQ/SYS_INIT/V4L2路径。不要在当前boot再启PAS，不用私有6帧测试替代V4L2/Codec2。证据out/video-decode/venus-transport/stop-sequence。

**内部池预算与收尾（2026-10-29）**：helpers两处DPB/internal dma_alloc及六处dma_free全部经sq2_intbuf_alloc/dma_free；SQ2与VB2共用原1GiB实验预算，非SQ2仍原DMA行为。分配前reserve、provider失败退款、直接free退款、退休保留charge到callback真实release；内部预算拒绝经stock NULL分支报-ENOMEM。实际helper/mock测试共享精确上限、超限不调用provider、失败/直接free退账及退休后release退账，ASan/UBSan通过，生成源计数2/6并拒绝残留原DMA调用。另按buffer所属不可重置scope->ended判定允许回收仍标FIRMWARE的DPB；SQ2 vdec_session_release五次abort决策改累计，旧平台不变。后两分支仅scope helper测试+生成回归/编译，尚非完整close运行验证。v1/v2三模块与原回归通过，原Module.symvers缺失/未上机。验证脚本首次找错host test位置exit1，从已完成stage复制到记录目录后重跑通过。**下一步**：对实际vdec stop/output/capture/flush路径做闭环主机时序测试（特别close抑制flush_done后是否仍等reconf），核查streamoff-only与terminal worker锁序；通过后完成当次导出符号/模块依赖闭包及新boot一次性的IRQ/SYS_INIT/V4L2真实帧测试，不再重复已完成的小向量或预算检查。Android Codec2/逐codec对齐仍开放。证据out/video-decode/venus-transport/internal-budget。

**会话代次回收接入（2026-10-29）**：sq2-venus-retire新增独立refcount scope与不可重置ended状态。HFI session init建立新scope（旧未确认拒绝替换）；在实际VB2 process、DPB process、内部SET三个地址发布点绑定，旧scope确认后活动对象可重绑定新scope；不是按分配时固化。专用END成功路径scope_end只摘取该代已退休项；晚到旧代项即时释放，其他会话失败项继续等PAS；instance destroy drop所有者引用，entry持引用直到真实回收。内部成功unset/realloc改转退休链，SET失败但已绑定者也保留，避免丢scope引用/过早free；未插链intbuf处理零list。实际wrapper/mock测试覆盖两会话、旧代晚到、活动对象跨代复用、失败会话在drop owner后仍保持至PAS，全程ASan/UBSan，原2000竞争/200callback/END等及三模块编译/4生成回归通过。source scope_api False/True/False对照及patch/rollback哈希通过；不是硬件实测。新增END mock stub时替换误及Pythonmarker，工具检查后已修正，最终测试通过。**下一步**：内部/DPB仍无预算，补入共享预算；审查END后仍标记FIRMWARE的DPB及streamoff-only/error-flush等待/错误累计，明确真实V4L2测试前条件。缺Module.symvers/未设备访问限制保持，不能宣称Android硬解。证据out/video-decode/venus-transport/session-scope。

**VB2保留预算（2026-10-29）**：审查会话代次时发现helpers/vdec错误路径也会SESSION_END，活动VB2对象可能保留并复用，不能仅按分配时代次在END后无条件回收。先给实际SQ2 VB2 allocator接入core锁保护的1GiB实验预算，live+retired driver-held backing均记账；MMAP按分配大小，进口DMA-BUF按exporter完整size（不是请求plane size），provider失败退账，退休不退账，实际release时退账。零/超限/溢出拒绝-ENOSPC，PAS停止后新分配-EIO；该预算不是硬件能力或系统总内存限制，**内部/DPB池尚未计入**。实际wrapper/mock检查full exporter、失败退款、退休保留、晚到释放、post-stop拒绝以及原2000竞争/内部转交通过ASan/UBSan；三模块及4回归通过，缺Module.symvers限制不变，无设备访问。source四角色budget_api False/True/False、exit0，非三次硬件试验；最初apply_patch上下文顺序错误，校正后完成。下一步会话scope必须在实际HFI提交时绑定/更新（覆盖会话结束后复用活动VB2对象），独立refcount并按确认END回收该代退休项；失败直到PAS停止；补内部池预算、streamoff/error-flush审查后才上机。证据out/video-decode/venus-transport/dma-budget。

**正常回收的END确认前置修复（2026-10-29）**：审查发现stock hfi_session_deinit在sys_error绕过发送仍返回0，且共享inst->done可由其它回复完成，不能拿其ret0释放退休DMA。SQ2现使用独立sq2_end_done/error与sent/confirmed字段，只有END handler完成；sys_error返回-EIO，不相关回复不能确认，失败/超时END不重发且阻止后续session init；真实成功确认后才允许下轮init重置确认状态。未据此提前释放任何buffer。实际生成deinit及END/load handlers以mock回复执行10组合：BASELINE/ROLLBACK sys_error及不相关回复均ret0；MODIFIED分别-5/-110、confirmed0；正常END、错误回复、发送失败/重复调用通过ASan/UBSan。三模块/原回归通过；缺Module.symvers、无设备访问。对照测试初次unused timeout stub触发Werror exit1，令legacy mock等待实际调用该stub后重测通过。**下一步直接接会话代际拥有的retirement fence**：不能把可重置的inst->sq2_end_confirmed直接附给旧buffer；必须用独立引用计数scope，成功END释放该代已退休项、晚到项即时释放，失败保留到PAS停止；同时加全局有界分配计数。随后streamoff-only/error-flush审查、新boot V4L2及Android Codec2。证据out/video-decode/venus-transport/session-end。

**HFI实例回调引用（2026-10-29）**：固定hfi.c/hfi_msgs.c哈希；SQ2 to_instance在core锁内增加callback计数，handler之后put；session_destroy先从列表移除，解锁后drain。vdec_close最前标记closing并等待在途回调，后续新EBD/FBD/session event拒绝，控制回复仍允许；FLUSH仍complete但closing不调用flush_done，core system-error广播跳过closing实例。最后put先保存core、只唤醒core长寿命waitqueue，避免最后引用归零后访问已释放inst等待队列。实际inline helper用mock waitqueue/POSIX线程跑200交错，验证close等待在途引用、帧/事件拒绝、控制回复保留、legacy不计数；ASan/UBSan通过，生成回归检查close-before-m2m、get/put及remove-unlock-drain顺序。三模块编译成功，缺Module.symvers不算ABI证明。首次编译缺hfi_msgs.h常量exit1，显式include后v2通过；交易验证首次Python引号错误exit1已修正。source transaction guard BASELINE=False/MODIFIED=True/ROLLBACK=False，全部exit0，仅生成源对照。未上机，**不是完整IRQ/VB2并发证明**；下一步补有界retirement/正常session安全回收，审查streamoff-only（非close）路径及error与flush等待，再新boot真实V4L2/Codec2与codec能力矩阵。证据out/video-decode/venus-transport/callback-lifetime。

**终止后resume入口修复（2026-10-29）**：审查确认venus_iface_cmdq_write与venus_set_state使用同一hdev锁，命令队列在DEINIT后已拒绝；但venus_resume原样会在DEINIT后再次venus_power_on。SQ2分支现将deinit的state/suspended/power_enabled一次加锁更新；resume在同锁内拒绝DEINIT或sys_error，然后才允许power/MMIO。初始零状态（create后、SYS_INIT前）仍允许首次resume，非SQ2保持原行为。实际生成函数抽取编译24状态组合ASan/UBSan通过；相同deinit→resume输入BASELINE/ROLLBACK ret0/power_calls1/suspended0，MODIFIED ret-5/power_calls0/suspended1，均程序exit0；补丁重建及回滚hash通过。全flags三模块编译、原测试与4生成回归通过；未上机、缺Module.symvers限制不变。**下一未完成点**：hfi_msgs.c::to_instance在core锁内查找后释放锁返回裸inst，handler在锁外使用；hfi_session_destroy移除列表后vdec_close销毁ctx/ctrl/inst的完整回调寿命仍须同步，不能直接全程锁住close（同步HFI命令等待IRQ会死锁）。需要实例callback引用/关闭阶段与等待点的实际实现/测试。之后有界retirement及正常session回收，再新boot V4L2/Codec2。证据out/video-decode/venus-transport/terminal-resume。

**DPB/残留内部buffer归属（2026-10-29）**：新增venus-sq2-intbuf.h，SQ2 free_dpb_buf转core retirement、不提前ida_free复用固件可见tag；vdec_close在hfi_session_destroy之后、kfree(inst)之前转交所有残留DPB（含FIRMWARE-owned）与内部buffer，回调只依赖buffer元数据及get_device引用、不依赖inst/IDA。helpers.h新增固定哈希。actual helper/mock测试将2个DPB+3个内部buffer转交后先free实例，再PAS确认drain，5个释放、device refs归零，ASan/UBSan通过；生成close-hook选择对照baseline/rollback core_owned0、modified5，三程序exit0，仅该helper模型，不是完整close实测。两轮三模块编译、32释放故障、VB2/retire2000项竞争、原故障测试与4生成回归通过；缺Module.symvers限制保持，未连接设备。验证脚本首次引用/tmp二进制已消失exit1，改为out持久路径重编译后通过。**下一步**：审查/修复terminal与streamoff/close/HFI回调锁序、确认停止后新提交全拒绝；补有界内存与正常session回收，当前保守buffer/tag保留不能用于长期播放。新boot真实V4L2/Android Codec2仍未完成。证据out/video-decode/venus-transport/internal-owner，原角色及观察保留。

**VB2 backing寿命接入（2026-10-29）**：固定vdec.c哈希，SQ2输入/捕获两队列使用venus-sq2-vb2.h memops，非SQ2保持原memops。MMAP backing与进口DMA-BUF的mapping/额外get_dma_buf引用/decoder模块pin独立于vb2_buffer与inst；VB2 unmap先保留同一mapping，detach/put转入core退休链。独立retire mutex避免再引入inst/core锁逆序；仅IRQ已同步且PAS stop成功后drain，PAS失败不drain，晚到的退休直接释放。actual wrapper/退休代码的mock ASan/UBSan覆盖申请/attach/map失败、重复map/unmap、VB2引用丢弃、正常回收、未映射detach、晚到退休；两个线程2000退休项与stop线程竞争各释放一次。core故障测试确认PAS失败无drain；三模块全flags编译、4项生成源回归通过。baseline/rollback选stock mock：close时refs0/mappings0/unmaps1/detach1；modified refs1/mappings1/unmaps0/detach0，PAS确认后归零。首轮生成q->mem_ops错误anchor与host puts冲突均exit1已修正。**编译/主机验证，不是可部署解码器**：未连接设备，缺原Module.symvers；当前保守地将所有退休backing留到terminal stop，尚需有界内存/正常session回收策略。下一步DPB与内部缓冲区转core-owned归属、terminal与streamoff/close/HFI回调完整锁序及禁止停止后新提交，之后才能新boot实机V4L2/Android Codec2。证据out/video-decode/venus-transport/vb2-lease，原角色不替换。

**内部缓冲区释放失败修复（2026-10-29）**：实际生成helpers.c的intbufs_unset_buffers与venus_helper_intbufs_realloc在SQ2 RELEASE_BUFFERS失败时立即返回原错误，保留失败及后续DMA/list，不继续释放或分配替代scratch；旧平台行为不变。实际函数抽取编译，32个mock用例覆盖两入口、SQ2/legacy、成功/三个失败位置、-110/-5，ASan/UBSan通过。相同首请求-110输入，BASELINE/ROLLBACK均错误释放3个并返回0，MODIFIED释放0、保留3、返回-110；三程序exit0，基线仅观察并非安全通过。补丁重建/回滚hash通过；全flags三模块编译及4项生成回归通过，缺原Module.symvers仍非ABI证明。未上机。**整体生命周期仍未修复**：stock vdec_close调用m2m_ctx_release，VB2会unmap/detach进口DMA-BUF；vb2_dc_attach_dmabuf没有额外get_dma_buf引用，不能只跳过detach而假定exporter存活。DPB free、终止worker与streamoff并发、session释放后内部链归属也未完成。下一步实现core-owned quarantine（含独立DMA-BUF引用/映射寿命）与串行化终止，再新boot V4L2/Codec2实测，禁止当前boot二次启动或加载此候选。证据沿用out/video-decode/venus-transport，新增internal-release四角色。

**SQ2资源表/decoder节点与buffer边界（2026-10-29）**：--sq2-resources需platform-entry，新增仅microsoft,sq2-venus-lab匹配；保护参数严格使用实测0/25800000/1000000/24800000，未照搬sdm845_v2的70800000。独立PM不使用stock频率/拓扑猜测。helpers.c哈希固定，DPB/internal分配及vb2 init/prepare在32bit HFI赋值前检查完整地址/大小，output payload检查offset<=used<=length；SQ2的DPB ida失败路径补释放未入链分配。10边界例与8192长度×6检查ASan/UBSan通过，三模块/原故障测试、4项生成源回归通过。--venus-decoder overlay仅含decoder子节点，精确SID/现有power/clock/ICC继承已验证配置；SPI174来自SQ1 ACPI GSI206与SM8150参考，**不是SQ2实时IRQ验证**。DTBO/模块已编译并fdtget复核compatible、interrupts=0 ae 4、唯一video-decoder，未加载；当前boot已有驻留HFI节点，overlay明确拒绝共存，需新boot。构建首轮替换anchor不唯一exit1，缩小到intbufs_set_buffer；DT首轮属性在子节点之后exit1，重排后通过，证据均保留。下一步审查并修正并发error/streamoff及PAS失败时V4L2用户/内部缓冲区寿命，再做新boot真实V4L2帧/Android Codec2，不可仅凭地址检查宣称DMA完整安全。

**Venus入口故障测试与修复（2026-10-29）**：实际venus-sq2-core.h在主机mock提供者下执行11个probe失败点、PAS停止失败保留、terminal error无重启、remove保留及opt-in拒绝。复现旧子设备创建失败留下pm_enabled=1；新增pm_runtime_set_active返回值检查与失败分支pm_runtime_disable。相同populate失败输入BASELINE/MODIFIED/ROLLBACK分别pm_enabled=1/0/1、ret=-5、children=0、retained=1，三者程序exit0；补丁重建与回滚哈希通过。三模块编译/原组件测试通过，主机logging stub初次未消费ret导致Werror exit1已修正。不是并发/真实IRQ证明；设备未加载，ABI缺原Module.symvers限制不变。下一步补齐固定SQ2资源表、decoder-only DT节点和V4L2 buffer DMA边界，审查并发终止/用户缓冲区释放后再实机；不重复私有H264向量当作Android硬解。证据out/video-decode/venus-transport/core-faults。

**Venus独立主入口（2026-10-29）**：--platform-entry需--firmware-lease，生成core.c在devm分配前分派独立probe，并分派remove/shutdown/runtime PM；sq2_platform_allow默认false、仍无SoC match。probe顺序为独立owner→power→HFI→PAS→HFI resume/IRQ/SYS_INIT→V4L2注册/子设备。错误worker只停止，不重启/重排任务/走stock coredump；PAS失败保留全部资源。发布前且停止成功可清理，发布后remove保留core/HFI/v4l2及module引用到重启，避免打开文件UAF；**非完整热卸载支持**。实验版保持供电、禁用FW自动power-collapse并拒绝父runtime suspend，不是功耗优化。ARM64三模块编译、已有实际C/mock故障测试及3项生成源隔离检查通过；新dispatcher尚无运行故障注入/设备验证。下一步测试probe各失败/并发错误/移除，补齐SQ2资源表、仅decoder子节点和V4L2 DMA输入检查，再进行新boot首次固件启动的V4L2测试，不能继续私有向量代替Android接入。原Module.symvers缺失限制不变；本轮无设备操作。证据out/video-decode/venus-transport/core-entry。

**Venus电源所有者（2026-10-29）**：sq2-venus-power.c移植实测ICC三路径→三域→四路复位脉冲→axic/axi0/axi1/ahb/core/vcodec/cvp→200MHz/版本校验顺序，保存CPU配置与原rate；确认PAS停止后恢复配置/rate、停钟、HANA桥接脉冲、复位原态、关域/撤ICC。独立platform owner要求power ready才允许secure begin，secure/quarantine禁止power-off，仍持power禁止最终release。实际power C用mock提供者覆盖40个申请/激活失败位置和回收、严格顺序、rate恢复失败保留供电引用；owner/PAS/DMA/生成源回归及三模块编译通过。初次host测试因clocks名称冲突exit1，改为clock_refs后通过。reset_status沿用已验证provider/regmap检查；未读TZ保护寄存器。**未加载设备**，缺Module.symvers仍非完整ABI证明；主core.c probe/recovery/remove与runtime PM尚待接通，明确probe门槛保持。下一步真正接通独立平台入口与错误路径，再做V4L2/Codec2；不在bootbcd0再次启动固件。证据out/video-decode/venus-transport/power-owner/build-v2。

**Venus独立资源所有者（2026-10-29）**：sq2-venus-platform.c不使用devm，独立持有core、MMIO region/mapping、初始禁用IRQ、device/module引用；PAS begin/end已从固件dispatch连接，失败进入sticky quarantine并屏蔽IRQ，拒绝释放及重新启用。正常释放还要求HFI priv/firmware owner清空。实际owner C在主机mock资源提供者下覆盖6个申请失败点与完整回收、失败保持全部引用及正常释放；ASan/UBSan通过，另有4种sticky失败/100正常寿命及原PAS/DMA/MMIO回归，三模块ARM64编译通过。前次host编译exit1为misleading-indentation，已修正并重测，不是硬件失败。**未实机加载、未完成平台入口**：power/reset/ICC尚未归属该owner，core.c probe/recovery/remove尚未改用独立路径，EOPNOTSUPP门槛保持；设备/overlay强制移除也未验证。下一步接入实测ICC→三域→复位→时钟与关闭逆序，并纳入同一quarantine，再接主入口/V4L2。build-v3与补充四角色在out/video-decode/venus-transport/platform-owner；原记录保留。

**用户更新锁屏策略（2026-10-29）**：用户表示已移除全部Google账户，并明确授权清除PIN、禁用锁屏。已对当前用户10通过locksettings清除凭据并set-disabled=true；实读is-user-secure=false、get-disabled=true。guard=Y下关屏3秒再亮屏，Keyguard showing=false、inputRestricted=false、SCREEN_STATE_ON/AWAKE，boot仍bcd0a17e…未变。此授权取代历史“保留PIN”要求；未更改30分钟屏幕超时、未重启、未写启动镜像。账户移除是用户陈述，未另行审计；跨重启状态尚未验证。凭据不入仓库。硬解platform-owner候选仍未完成：host测试因misleading-indentation编译失败待修正，不得标为通过。

**Venus PAS调用接入（2026-10-29）**：--firmware-lease现将venus_boot/venus_shutdown的SQ2分支连接到既有sq2_venus_fw owner，沿用固定固件及PIL gap、原资源保护参数；load/auth原错误直接返回，protect失败调用owner关闭，关闭失败保留owner且记录两个错误。每core的sq2_boot_attempted阻止再次启动（不是跨模块重载的整机锁）。八种boot/protect/stop故障组合、三种非法平台、已有事务/DMA/MMIO及生成源opt-in测试通过，core/dec/enc编译成功。**完整外围隔离尚未完成**：core.c的SQ2 probe显式EOPNOTSUPP且仍无match，防止通过stock错误/恢复/remove路径释放devm/IRQ/HFI。下一动作是把这些资源改成独立拥有的SQ2上下文并接入已验证power/reset，而非解除probe门槛；之后才实机V4L2/Codec2。无设备访问或固件重启，缺Module.symvers的ABI限制不变。补充证据out/video-decode/venus-transport/pas-integration。

**Venus DMA平台接入（2026-10-29）**：实际hfi_venus create路径在队列分配前校验2300/2320/2340精确SID、DMA API domain及单设备IOMMU group；保存32bit mask/coherent/bus limit，保持更严格原limit，分配失败或释放全部队列后恢复。UC检查1MiB对齐/交集范围及全部4096页翻译；不改SCM范围。ASan/UBSan覆盖各SID/域/组/部分mask失败与恢复、两端合法映射及4096个缺页位置；ARM64 core/dec/enc编译和生成源隔离测试通过。仍无平台match/设备加载，缺Module.symvers不算ABI通过。stock core.c恢复会忽略venus_shutdown结果并重启，probe/remove也继续释放devm资源：下一步必须解决PAS失败隔离、power/reset与错误恢复/解绑寿命，再启用平台，不能只替换firmware.c调用。原证据保留，补充在out/video-decode/venus-transport/dma-integration。

**Venus传输集成推进（2026-10-29）**：prepare-sq2-venus-iris1.py新增显式、默认关闭的sq2_windows_hfi资源标志；将已实测CPU-only六邮箱、跳过VERSION/DSP写、单个16MiB coherent UC及SFR子视图接入真正hfi_venus.c调用点。IOVA范围检查拒绝越过25800000..c0000000，SFR不重复释放；原平台表不变、未添加match。core/dec/enc ARM64模块编译成功，MMIO/边界ASan/UBSan、事务及实际生成源隔离测试通过；缺原Module.symvers，仅编译通过，不是ABI或设备验证。baseline/modified/rollback生成源对照均exit0、补丁重建及回滚哈希通过。未访问设备或再次启动固件。下一步：集成精确SID/DMA aperture与已验证power/reset/PAS owner，重点处理core.c错误、恢复、remove的隔离寿命，再启用平台；Android Codec2仍待接入。补充证据out/video-decode/venus-transport，不替代原hfi-decode四角色。

**首次真实H264帧验证通过（2026-10-29）**：接续既有未提交decode候选，修正runner collector240s/4MiB非法参数（原进程12064已消失、无日志/模块，尚未固件启动），收紧bufreq多属性/尾部残片/超容量检查及分配大小溢出保护。21c883a3…在干净bootbcd0a17e…首次固件启动，实际解出320x240 Constrained Baseline level1.3的6个I/P帧，12个Y/UV CRC全部等于ffmpeg独立参考，verified6/6/mismatch0、EBD7无错、EOS1；STOP/RELEASE/SESSION_END/PAS关闭及全资源回收0，rmmod0。boot、PID2201/1973、guardY保持，pstore空、无SMMU fault/WARN。正常镜像未改，提供者与overlay驻留，不热拆、不在此boot二次启动固件。用户目标已扩展“Android可用并对齐硬件能力”：configs/sq2-video-decoder-capabilities.json逐codec区分固件声明与实测；Android media.player仍只有软件视频decoder。**下一动作：把已验证平台/PAS owner/SID/IOVA/邮箱行为接入现有Venus/V4L2准备链，先编译审计，再开放Codec2与逐codec能力对齐。不要继续只做私有向量或改XML冒充Android硬解。** 见VIDEO-DECODE新首节。

**首次H264解码session往返（2026-10-29）**：正常重启到干净boot44c03275…，本boot首次固件启动下994a00f5…实测SYS_INIT 75属性、SESSION_INIT/END（cookie53513201）均error0，全部关闭/回收0、rmmod0，boot/PID1976/2304、guardY保持，无fault/pstore。离线parse-sq2-hfi-caps.py逐条解析75条=声明数、774/774字；H264解码OUTPUT仅NV12_UBWC，OUTPUT2含线性NV12，96..8192。不是帧解码。同boot第二次固件启动失败，热重启开放：每boot只启动一次固件。**下一动作：session内属性设置/缓冲区需求查询→分配缓冲区→已知H264码流→验证输出像素。** 见VIDEO-DECODE首节。

session候选实测（2026-10-29）：boot39906e2a…内单次运行994a00f5…，到CTRL_STATUS前与成功轮相同，但控制状态保持0、SFR空，-110；新caps/session代码未到达。PAS关闭及全部回收0、rmmod0，boot/PID/guard保持、无SMMU fault、pstore空。这是同boot第二次固件启动，重复上电稳定性开放；不要在本boot重试。**下一动作：用户同意后正常重启，干净boot首次固件启动只跑一次run-hfi-session.sh。** 见VIDEO-DECODE首节。

能力体/H264 session候选（2026-10-29，已在同boot二次启动实测，未到达新阶段）：消费者新增opt-in `hfi_caps=1`（整包有界复制SYS_INIT回复、原样RAW日志与上游逐字扫描汇总）和 `hfi_session=1`（仅在dec掩码含H264时SESSION_INIT/END一次，无缓冲区/码流/帧）。环复制/写入、caps解析及事务主机ASan/UBSan通过，`994a00f5…`对39906e2a…导出missing=0。未部署，无session或解码证据。**下一未完成动作：确认设备boot/驻留提供者后只跑一次out/video-decode/hfi-session/run-hfi-session.sh并取回日志；不要重复已验证的DMA/SYS_INIT门槛。** 见VIDEO-DECODE首节。

独立DMA清理告警补正（2026-10-29）：审阅完整raw日志发现baseline/modified/rollback三次of_overlay_remove虽返回0、sysfs节点消失，却均有OF引用计数/内存泄漏告警，首次另有refcount saturation WARN。4096页DMA映射/解除映射证据有效，但不能称kernel lifetime完全清理；不再重复该热拆路径。独立builder默认拒绝，源码仅保留取证；未修复其引用寿命。HFI路径使用驻留overlay、不热拆，后续沿已成功传输推进能力体/session/真实帧。本boot曾出现诊断WARN，不能冒称无内核告警或长期稳定；不要因SSH/桌面仍正常就抹去该限制。

**首次真实HFI成功（2026-10-29）**：独立16MiB DMA对照已实测legacy1300/mask60、精确2300/2320/2340及哈希回滚副本，均4096页映射/CPU读回/全部解除映射、overlay/module清理0。随后显式Windows HFI profile（同三SID、24/192表、16MiB UC/IOVAbf000000、只写CPU邮箱、跳过该路径未用VERSION写；SCM保护范围不变）在原42版固件4e93d345…上实返CTRL40000001、空队列rx_req0→1、SYS_INIT命令r/w3/3、3112字节回复/error0/properties75、msg r/w778/778。PAS关闭0、队列解除映射、HANA退出桥接脉冲0、ICC/时钟/域回收0，消费者refcnt0后已卸载。boot39906e2a…、PID2295/2104、Android1/guardY保持，正常镜像不变。提供者与两overlay仍驻留，不热拆。93cd5b7d…消费者及a8e7c9ff…overlay已实测；这是实际VPU共享队列通信，不是解码帧/Android组件。组合测试不能单独归因SID。**下一未完成动作：有界提取/解析SYS_INIT能力体，然后真实H264 session创建/销毁与帧解码；不要重做已完成的DMA/采集门槛或退回旧1300全流程。** 见configs/sq2-video-hfi-status.json和VIDEO-DECODE。

Windows初始化静态门槛通过（2026-10-29）：inspect-sq2-windows-video-reference.py固定14369cb4…驱动/acfe9033…IORT，SQ1和原SQ2输入结果一致，六项指令检查及十映射核对通过，9项正负例检查通过。直接指令确认Windows表头24/总表192、qheader56/3队列；UC大小16MiB，基址为对象两个u32字段和减16MiB（字段语义仍需追踪）；CTRL_INIT写1。不是设备compact/16MiB通过。上一轮为progress，本轮无VPU/固件/设备状态改动。下一未完成动作：建立显式2300参考SID的独立DMA候选并验证分配/映射/解除映射，先不接电源/时钟/MMIO/PAS，正常启动配置不变；不要退回重复收集或凭静态结果宣称硬解。详见SQ1-VIDEO-REFERENCE。

SQ1参考采集与SQ2日志续接（2026-10-29）：已核验用户提供的SQ1 SSH指纹并公钥登录，确认为Surface Pro X/SQ1/ARM64/Windows22631管理员。只读导出5包、95设备资源、8张校验和有效ACPI；取回71文件全部哈希通过，原始包仅private。SQ1的qcdx包与原SQ2新版Windows包共有34文件全相同，固件为79版891997ec…；当前Android实测仍用42版4e93d345…，未替换。SQ1 DSDT/IORT给出VIDEO引擎4以及指向SMMU15000000的2300/2320/2340等映射，与当前从sdmshrike继承的1300/mask60及高IOVA有差异；须离线核对本机SID/保护范围，不能盲搬或宣称根因。旧empty-76f0日志已取回，九次邮箱写完后最后为HFI_BOOT begin，无状态/队列/关闭结果，不再是注册/ICC未知。当前只读boot39906e2a…、Android1、guardY/nohibernate、PID2295/2104、临时模块清空/pstore空；恢复方式未确认，保留用户此前“桌面触摸正常”，不得称整机冻结或自主重启。本轮无新VPU试验/启动盘修改。详见SQ1-VIDEO-REFERENCE；下一步离线核对Windows同字节驱动的SID/IOVA和HFI配置，完整硬解仍未完成。

最新恢复与新日志待取（2026-10-29）：SSH已核对用户恢复后的boot76f0f9ca…、Android就绪、guardY/nohibernate、PID2264/2082、临时视频模块清空、pstore空。compact原日志已取回：只有ready/运行期噪声/smoke（602.714066s），没有新消费者日志或KMSG_END；原主机输出确认rmmod完成并打印COMPACT_BEGIN，但不能判断卡在模块注册、probe早期ICC获取还是后续尚未落盘步骤，不能据此否定24字节布局。已增加driver_register/probe_enter/ICC get前后标记。新5a967ca3…候选在干净boot上仅加载一次（默认288表头、empty_ping及HANA退出恢复），连接再次超时，**sq2-hfi-empty-76f0.kmsg待取，未取得共享队列写入/回复/退出结果**。不再启动VPU，先恢复并读取这份逐步日志确定实际阻塞阶段；未改启动盘。

操作者确认（2026-10-29）：用户已明确反馈24字节表头对照后“整机无响应，正在重启恢复”。这次是用户执行的长按恢复，不应记录为自主崩溃或自动恢复。当前重启完成、桌面/guard及新boot ID尚待只读复核，SSH仍在标识交换前关闭；不要复跑该测试，先恢复默认入口并取回sq2-hfi-compact-976c.kmsg。启动盘未改，完整硬解未完成。

最新离线协议证据（2026-10-29）：SSH仍在主机标识前closed，compact日志未取回，本轮无设备侧重试。新增inspect-sq2-video-firmware-protocol.py对固定SQ2固件校验六组ARM指令：实际用table+8的qhdr0_offset、stride56、CPU/DSP邮箱偏移、bit15启用/清除、空队列rx_req=1，以及CTRL_STATUS bit0可早于队列解析。故状态1不是队列DMA或完整启动证明，不应盲改表头/IRQ位。新hfi_empty_ping=1先将初始cmd.rx_req设0，空通知后观察固件写成1（且r/w=0），再继续SYS_INIT；406a979b…候选编译/边界/事务/导出审计通过，未部署。保留默认288表头及HANA关闭恢复候选；先设备恢复/取compact日志，再验证完整退出与真实共享队列访问。无解码帧/Android组件，未改启动盘。见VIDEO-DECODE。

最新HFI控制启动通过（2026-10-29）：已取回19ed…完整试验日志，PAS auth0/protect0后到queue-status before，随后进入邮箱快照读取前失联。新boot976cc99e…下203943ae…只写邮箱候选实测九次发布完成，CTRL_STATUS=1；SYS_INIT已发，但cmd_r=0/w=3、msg_w=0，3秒后-110。真实shutdown0、队列解除映射、三ICC清零、全部资源回收/refcnt0，boot/PID2295/2106保持。这是控制启动通过，仍无SYS_INIT回复或解码帧。随后218b6908…的24字节表头单变量对照再次失联，**sq2-hfi-compact-976c.kmsg尚待取回，不能认定进入队列阶段，禁止重跑该完整流程。** 源码核对发现HANA=0x50100000在参考驱动的固件卸载/停钟后还会脉冲桥接，旧探针仅恢复初始复位位漏了这步；已补版本限定post_reset，PAS关闭失败仍保留所有资源，编译/审计通过，未部署。先恢复/取最后日志再决定下一次测试；未改启动盘。见VIDEO-DECODE。

最新ICC/复位续接（2026-10-29）：768d0e3修订候选在boot861e77bc…再次失联，但取回native日志只到reset_deassert axi结束（377.420876s），未到时钟/PAS/HFI；不能归因为诊断寄存器。后只读确认新boot19edba8b…、Android/guard就绪，恢复方式仍未获用户确认。已按下游补上复位前ICC请求；三条路径真实解析/请求/清零全0，随后带trace_delay_ms=100的三域四reset、AHB六时钟/版本200MHz阶段均0并回收，PID2273/2079保持。完整HFI试验随后连接再次超时，**最新native sq2-icc-only-19ed.kmsg尚未取回，不要重跑fa09f750…全流程，先恢复/取日志。** 新203943ae…候选移除全部额外MMIO及邮箱快照读取，仅保留下游控制状态轮询，关闭成功后清零独占邮箱，增加逐次写入前的collector等待；仅本地编译/导出审计，未部署。没有解码帧或Android硬解，未改启动盘。固件来源已核对为SQ2原qcvss8180.mbn同字节，目录venus-5.2是原提取脚本命名。见VIDEO-DECODE、sq2-video-interconnect-reference.json。

最新HFI续接（2026-10-29）：独立SMMU消费者SID1300/mask60、DMA-FQ唯一peer、3MiB IOVAdfc00000及768页映射/CPU读回/解除映射实测通过，不是VPU DMA。首轮PAS仍全部0，HFI CTRL_STATUS始终0、-110，未发SYS_INIT；真实shutdown及全部资源回收通过。第二轮mem_protect_video_var返回0，随后扩展诊断在读完e3000后失联，下一个计划读e1020（TZ阈值）；不得复用65709696…二进制或非安全读取e1020..e1034。源码已移除阈值/reset读取，新候选仅编译/符号审计及ring边界ASan/UBSan通过，未部署。SSH后来读到新boot861e77bc…、Android就绪、guardY、PID2293/2110、临时模块清除、pstore空；本轮恢复方式/桌面确认尚待用户答复。未改启动盘。下一步有界collector下验证安全诊断和保护设置后的真实HFI启动，无解码帧或Android硬解。见VIDEO-DECODE。

最新视频PAS通过（2026-10-29）：只读SCM窗口查询实返8b700000..9ab00000，旧fd000000在外；Surface原DTS明确记录8bd80000..8d800000 image-loader空隙。新私有pil_gap仅在该空隙固定申请8c000000..8c600000，检查live SCM窗口、无System RAM/DT reserved重叠、generic reserved父项及无其它资源owner，先无写入claim测试，再实机init_image/mem_setup/auth_and_reset/shutdown全部0，raw安全返回也0。不是占CAMERA或未知保留块；记录configs/sq2-video-pil-memory-reference.json。消费者/trace/辅助设备/owner已回收，boot/PID/guard保持；**还无HFI响应/解码帧/Android组件，目标继续进行。** 下一步SMMU合法IOVA和SYS_INIT握手。原日志out忽略，软件VIDEOCC/overlay保持临时驻留，禁止热卸载提供者/全局genpd汇总。见VIDEO-DECODE。

最新视频bring-up（2026-10-29）：干净boot b7730aab…直接用软件控制VIDEOCC，已实测桥接四reset状态/顺序/恢复、AHB及六时钟，实际读取VPU版本5010002f、mask1f6、ctrl0；200MHz档驱动报200000097Hz并可恢复19.2MHz。实际PAS加载已到mem_setup：init_image返回0后mem_setup拒绝本次独占DMA区fd000000/5MiB跨度，-22；未到auth/未启动固件。真实shutdown返回0，DMA/设备及全部消费者资源已回收、消费者卸载；boot/PID保持、guardY。支持查询对PAS9和已运行PAS1/18/4全false，现仅作诊断，不虚报成功。下一步查SCM加载内存契约，不占CAMERA/未知reserved区。overlay与软件VIDEOCC临时驻留，不热卸载或读全局pm_genpd_summary。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

最新分阶段视频实测（2026-10-29）：独立有界kmsg确认0资源、三GDSC、GCC三AXI和MVSC时钟可申请/启停回收；原VIDEOCC在VCODEC时钟真实报stuck-off/-16并正常回收。仅将两codec域暂留软件PM控制（不改频率、不跳过halt检查）的候选24ed5d48…，三域六时钟的实际probe_result=0，回收计数0。未读VPU MMIO/未执行PAS。**后续全局pm_genpd_summary读取陷入不可杀内核循环，Android/SSH仍活；sync后默认受控重启已实核恢复：boot b7730aab…、boot_completed=1、screen_only=Y，临时模块/节点/任务清除，pstore空。不要热卸载GDSC提供者或重读该汇总。** 下一次干净启动直接加载私有候选，先补下游桥接reset流程，再读寄存器。前次v3整机无响应的根因仍未证明。详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

最新视频试验失败（2026-10-29）：独立VIDEOCC+GDSC/clock/version探针v3测试后用户确认整机无响应，手动长按重启恢复。未获版本/清理结果，具体阻塞阶段未知，不要原样重试；无PAS/HFI/固件启动或启动盘写入。恢复后SSH实核新boot9435bddc…、boot_completed=1、screen_only=Y、PID2242/2098、pstore空，临时模块/节点已清除，用户确认桌面恢复。v4仅本地编译，默认0域/0时钟/不读MMIO并加逐步标记；下次需先独立有界kmsg采集、逐条命令标记，从零上电阶段定位。SSH本次用ecdh-sha2-nistp256/aes128-ctr连通，主机校验和原身份保持。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

固件寿命续接（2026-10-29）：独立PAS9内存事务/所有者已编入私有候选（`--firmware-lease`），真实调用前标记secure touch，load/auth失败也需成功shutdown才释放；失败隔离同时持有DMA、独立设备和模块引用，禁止重试/重分配。16种主机故障组合及100次正常复用的ASan/UBSan检查通过，编译通过；**无启用调用点、未执行PAS**。core.c错误/恢复/remove仍释放HFI及devm资源，需整体处理后才能接入，不要只替换固件分配。目标仍为实机解码与Android组件，详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

Iris1 HFI续接（2026-10-29）：固定下游vidc_hfi_io.h哈希，新增独立iris1资源标志及CPU时钟、DSP镜像队列、IRQ掩码适配候选，保持HFI4xx和原平台表。隔离编译core/dec/enc及ASan/UBSan mock通过，三模块对同boot运行导出的符号闭包missing=0；未部署或添加SC8180X match。电源/reset/资源验证、低端IOVA保留和固件关闭失败的内存寿命仍开放；没有硬件解码帧/Android组件。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

视频硬解续接（2026-10-29）：`codex/sq2-video-decode` 在30aee11基础上新增固定固件MDT copy-only探针。现有4e93d345…固件在新分配低4GiB/1MiB对齐的6MiB连续DMA内存中，三个LOAD段/5MiB跨度逐段复制及BSS读回实机通过；内存/辅助设备已释放、模块已卸载，无allow参数负测被拒绝。23个未定义符号匹配运行导出，boot ID及system_server/SF PID保持、boot_completed=1、screen_only=Y。未调用SCM/PAS、未启动VPU，没有解码帧或MediaCodec硬解；下一步仍为Iris1电源/HFI和关闭失败时的内存保留策略。未写启动镜像或codec配置，详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

eDP最终回退补验：用户恢复连接后，SSH已核对原msm完整哈希f69c91e7…、boot_completed=1、screen_only=Y、默认闭源入口无测试/debug参数、一次性env=0。实时链路恢复2.7Gbps×4、2880×1920@60/355500kHz/24bpp；上一轮v3日志已取回out。下文“最后重启后未复核”缺口现已关闭，无新增切换或重启。

eDP v3实测更新（2026-10-29）：恢复模块依赖后已到boot_completed=1/SSH，候选哈希6293fa5d…匹配；驱动读数5.4Gbps×4、仍为2880×1920@60/24bpp，但用户报告黑屏/花屏，DPU持续vblank/commit/frame-done超时。不能称链路画面通过或90/120Hz可用。已保存状态、确认一次性env清零后远程sync+重启，用户确认桌面恢复；最后重启后的SSH未重连，未再次独立核对模块哈希。默认未提升，保留三菜单和已完成EFI清理。详见DISPLAY-REFRESH。

eDP 新分支 `codex/sq2-edp-link-rates`（2026-10-29）：只读 DPCD 确认无 DSC 声明，但 eDP 1.4a 速率表最高 5.4Gbps，旧字段仅2.7Gbps；已补候选协商代码。前两次候选启动因重编 msm 缺失 depends 元数据而未加载，尚未测试到链路；自动回退后桌面/原模块恢复。v3恢复原依赖、有界本地日志，测试中，不能称5.4通过。用户指出诊断Wi-Fi flyer-test已不适用，后续不再使用该配置。用户另明确要求仅保留闭源Vulkan、原Mesa、当前调试三个菜单项，已实施；10个无引用旧initramfs经本地哈希备份后删除，EFI可用1,486,316KiB。默认内核/DTB/闭源与Mesa镜像未改。详见 [DISPLAY-REFRESH.md](DISPLAY-REFRESH.md)。

启动集成完成阶段验证（2026-10-29）：runtime-v8在v7上关闭成功调用统计；新增启动助手、post-fs-data Direct配置、固定哈希打包器与离线测试。独立镜像 `5ba59bd9…` 已通过一次性真实重启，系统自动加载闭源HAL、1.1声明与Direct配置，Mesa EGL保持；SSH公钥重连正常，用户确认桌面/触摸正常。普通APK全屏120帧、计算/图像/光栅读回通过；Wild Life 5859分/35.09 FPS，40秒采样及用户目视正常，结果包SUCCESS。相同镜像已提升默认菜单，原fa64305d… Mesa镜像仍供救援及图形回退入口使用，同镜像还有 `sq2.vulkan=mesa` opt-out。未进行第二次默认入口重启或实机opt-out启动；完整标准一致性/更多格式仍开放，不能称CTS认证。01:24 system ANR另定位到Google提醒同步onStopJob超时，抓栈主线程在AppCompat配置处理，详见QUEST2-VULKAN-ONLY；未禁用用户同步。

颜色格式后续（2026-10-29）：runtime-v7在v5上增加真实RGBX/BGRA/FP16元数据适配，修正donor的BGRA新建导出format0描述。多格式AHB导入/导出、GPU与CPU读回、释放后引用及原窗口/多队列回归通过，尺寸至2880×1920；全屏FP16导入20轮触发25秒限时，仅独立3轮完整通过。临时系统接入时版本XML校正为1.1，PackageManager实测4198400；普通APK全屏120帧和Chrome实际ANGLE/Vulkan网页通过。新增格式交换链和完整CTS仍未验，v7未重跑Wild Life。已手动回退Mesa及原XML，Direct配置移除、测试锁释放，screen_only保持Y；正常启动镜像尚未修改。脚本/精确矩阵见QUEST2-VULKAN-ONLY。

当前目标：保留 Mesa EGL/GLES，仅替换系统 Vulkan 后端。闭源 EGL/GLES 研究保留在分支，不再切换桌面 GLES。

后续进展（2026-10-28）：已定位Wild Life交换链失败为适配层只接受UNORM、拒绝sRGB。版本5实验候选补充sRGB，12帧窗口中间亮度编码读回通过；无Direct配置仍花屏，加同版私有Direct配置后5876 / 35.19 FPS，采样和用户目视正常。已手动回退Mesa、移走临时配置，未持久化。用户允许Vulkan-only关闭120秒回退，新增显式 `--start-manual`；具体边界和复现见QUEST2-VULKAN-ONLY。下文失败记录为修正前历史。

最新进展：多队列重复释放已有候选修复；普通 APK 的外部句柄错误已定位为 `/proc/self/mem` 权限拒绝，改用有边界检查的 `process_vm_readv` 后，2880×1920 三角形完成120帧，截图正常。Chrome可正常打开，前八个被捕获的 Vulkan 管线创建成功，但后续部分管线仍有链接错误，不能称为Chrome整体失败，也不能据网页可见认定全部Vulkan路径通过。

**历史 Wild Life 失败（修正前）**：应用曾弹出“基准测试运行失败”；已由上方sRGB + Direct实测更新，保留失败证据见 [QUEST2-VULKAN-ONLY.md](QUEST2-VULKAN-ONLY.md)。

运行候选后续：runtime-v3 已补充真实RGBA8 AHB新建导出，64²16轮、257×129/2880×1920各3轮GPU读回及释放后双引用存活通过；导入再导出16轮、两种窗口格式及四队列150轮回归通过。早期导出测试漏声明export位，已修正重测，不引用旧结果作为标准通过。系统临时接入后普通APK120帧通过，Chrome可显示网页但GSL报缓存请求无法转换（flags 0x0c0c2600）；该错误在DRM分配函数拒绝分支，日志中的kgsl字样不证明使用KGSL。已回退Mesa并卸载Direct配置，boot/SF保持。更多格式、缓存分配、Chrome历史管线、能力XML与持久化仍开放，详见同页。

独立内存矩阵进一步复现：六类实际公布内存中type2（properties=0xb）4KB合法分配返回-3，其余五类分配通过，只有buffer兼容的type0/4另有GPU读写覆盖。GSL旧DRM缓存转换拒绝0x0c000000，缓存维护入口也不完整；需要建立实际调用关联并补足缓存语义，不能仅放行或偷换type。原始日志/反汇编仍在out，矩阵明确失败，默认系统保持Mesa。

后续缓存适配（2026-10-29）：有界GSL调用跟踪确认type2与Chrome失败均为flags=0x0c0c2600；当前MSM 1.10.0内核实际接受CACHED_COHERENT分配。runtime-v5在固定GSL拒绝分支接入真实硬件一致性缓存，另按真实HOST_CACHED属性为type4选择同一分配模式，未更改公开type/index或伪造cache维护成功。六类内存分配通过，buffer兼容type0/4各150轮4KiB及16轮16MiB GPU读写通过；type2/3仅有CPU映射/cache API覆盖。AHB导入再导出16轮、新建导出16轮、sRGB窗口12帧、普通APK120帧通过。Chrome本次网页采集未再出现原缓存/管线错误，不能覆盖历史所有路径。runtime-v5完成Wild Life 5813分/34.81 FPS，7.1/35.3秒采样正常，用户明确确认“这一轮画面正常”。随后手动回退Mesa、Direct目录恢复为空；boot/SF保持，zygote按计划重启。启动镜像未改，更多格式/能力声明/持久化仍待完成；详情见QUEST2-VULKAN-ONLY。

更新时间：2026-10-29。不包含原始日志、网络地址或凭据。

## 最近完成：共享存储

故障：Files、Chrome 下载及依赖 external_primary 的功能失败。/data ext4 已读写挂载，约 453 GiB 总容量；当前用户 10，用户 0/10 已解锁，但两个 emulated 卷均 UNMOUNTABLE。Settings 抛出 Volume external_primary not found。

根因：CONFIG_FUSE_FS=m，原启动流程未加载模块，缺少 /dev/fuse。运行中补载没有让失败卷自动恢复。修复在 probe/test2/init-googlebook-native 中于 Android init 前加载 FUSE 并检查设备节点，未格式化或重分区。

修复已写入当前内置 SSD，正常音频入口重启后两个卷均 MOUNTED。**用户确认系统“文件”打开下载/新建文件夹和 Chrome 下载均正常。** Android 接管后 /dev/fuse 为 root:root 0600，仍工作正常，不应据此盲目放宽权限。修复后 /data 约 450 GiB 可用。

现场 sm 独立 app_process 命令曾无输出并返回 0，不能据此判断挂载成功；以 dumpsys mount、实际 FUSE 挂载和应用访问为准。PID 1 的 /storage 绑定用户 0，从此命名空间直接访问用户 10 可能 Permission denied，不代表用户 10 应用故障。

## 当前待办

- 新跟踪已把实际阻断缩小：临时关闭预热后，SF以闭源Adreno启动；接入框架后因36×36 BGRA(format5)导入abort，SystemUI/登录app还混用了系统另一版本QGL编译器并SIGSEGV。独立75秒内核证据与busy→lazy回退均取得，boot保持，本轮无需手动重启；显式重启zygote后两个SystemUI实际maps已恢复Mesa。`combined-v4`用独立GLNEXT/QGL名字隔离配套编译器，两顺序/FP16/Vulkan/414读回私有回归过，实际HWUI尚未复验。下一步BGRA/RGBX语义与普通app命名空间绑定；不能以关闭预热或私有探针过关宣称系统替换完成。见 [QUEST2-SF-TRACE.md](QUEST2-SF-TRACE.md)。
- FP16独立适配已通过：`combined-v3`仅替换mapper3为`e9eaf753…`，保留真实分量与步长校验并隔离私有C++ helper ABI。FP16 384²连续150轮、13×7/全屏各3轮bit一致，RGBA8合成150轮、Vulkan/AHB、414读回和两顺序共存通过。第二次实际SF切换仍黑屏/卡住，用户手动长按恢复，已复核正常Mesa/boot_completed及关屏guard；pstore空、未留SF tombstone，根因尚未确定，不能宣称桌面通过或自主board crash。新watchdog持久化/忙挂载回退尚未设备验证。见 [QUEST2-FP16.md](QUEST2-FP16.md)，不要原样重复全局切换。
- 首轮真实SurfaceFlinger闭源切换已执行：Skia shader-cache预热要求384×384 RGBA_FP16(format22)，当前桥接拒绝后RenderEngine abort。独立watchdog恢复原Mesa哈希，SSH/框架恢复、boot ID保持；未写启动镜像。真实SF明确证明FP16现为启动必需，不能继续仅凭桌面缓存RGBA排除它。四种color格式真实PlaneLayouts已采集；下一步组件级FP16准入/步长及GPU采样验证，再重试SF。见 [QUEST2-SURFACEFLINGER.md](QUEST2-SURFACEFLINGER.md)。
- `combined-v1`14库已在同进程同时持有GLES context和Vulkan device，两加载顺序各GLES150轮+VK36帧、全屏各GL3轮+VK36帧全过；公共库唯一实例及13项真实GOT绑定通过，资源最终归零，原AHB回归正常。prepare逐字复制v4九库+fence-prune五专属库，不改任何ELF。见 [QUEST2-COMBINED.md](QUEST2-COMBINED.md)。下一步系统格式/DATASPACE、降低正常日志和真实SF可恢复切换；完整替换与性能功耗尚未验证。默认Mesa/启动镜像保持。
- GLES server-wait历史依赖累积已有实测候选：`gles-linked-fence-prune`只以原GSL wait(old,0)确认旧pending全部完成后释放该context的持有，再执行原clone/merge。未完成/错误保留依赖，无阻塞等待。64×64连续150轮、640×360/2880×1920各3轮异步合成像素全过、FD稳定11；414读回、36窗口帧、YV12三帧回归通过。GLES `1d3ba33e…`，prepare/QEMU/复现见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md) 第7节。下一步合并v4公共库、同进程GLES/Vulkan验证和真实SF；以下累积失败为前版记录。默认Mesa与启动镜像保持。
- GLES多层合成探针已实现，64×64/全屏单轮像素与native fence通过；连续运行发现每轮2个已完成sync_file积累，直到EGL销毁才释放。CPU等待、以及CPU等待后仅import/destroy而不server-wait的两项3轮对照均无增长，将问题缩小至GPU server-wait内部持有。默认严格测试仍失败，尚不可接管长期SF；对照明确DIAGNOSTIC_ONLY。见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md) 第7节。
- v77 Vulkan raw WSI 已完成有限实机适配：`v77-vulkan-ahb-v4` 以独立 mapper5 import 持有原 buffer，完整复制当前 loader 的 72 字节节点，按真实 donor image/memory 唯一 owner 检查后释放。64×64 连续150周期/1800帧，以及13×7、257×129、1920×1280、2880×1920各36帧全部像素/native fence通过；每周期视图/字节归零、sticky failure=0，原AHB回归通过。实机使用create时导入，deferred bind仅离线/mock覆盖；普通别名/其他格式仍有限制。见 [QUEST2-VULKAN-WSI.md](QUEST2-VULKAN-WSI.md)。正常Mesa/boot/框架保持，尚未系统替换。
- 闭源GLES读回已取得可验证修复：原库CPU转换选8通道/bank16，与实际缓冲区4通道/bank15不符；另有PBO优化截断非对齐offset和零高度范围计算下溢。当前最佳私有候选`gles-linked-zero-area`（GLES `c4796847…`）保留原阈值，用CPU4/bank15、PBO原fallback及局部零面积校验修正。基础414项+两组bank边界各54项，共522项全过；上传/采样五尺寸两phase各10项通过，YV12 client/NV12 PBO各3帧通过。默认Mesa、boot和框架保持，未写启动镜像；PBO回退可能降低性能，其他格式/SF与完整系统替换仍未完成。见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。零阈值候选已实测无效，不提升为默认。
- v77原配Vulkan的AHB适配已打通有限RGBA路径：`v77-vulkan-ahb-v3`九库通过真实mapper5元数据生成124字节QTI视图，公开入口事务与donor最终acquire/release分开管理，base=0由donor管理mmap。属性/import各150轮通过；4×4奇数边界RGBA图案64×64连续150轮，13×7、257×129、1920×1280、2880×1920各3轮逐字节通过，引用/视图最终归零，基础compute/image/raster仍过。原HAL close=NULL曾被wrapper误拒，v3已正确缓存；不是GDPA问题。导出、raw WSI、其它格式及完整系统替换未完成，当前探针仍保留应用AHB引用。见 [QUEST2-V77-VULKAN.md](QUEST2-V77-VULKAN.md) 和 [生命周期审计](QUEST2-VULKAN-AHB-LIFETIME.md)。正常Mesa、boot和框架保持，未写启动镜像；下一步为WSI寿命/交换链与GLES多层native-fence合成，见QUEST2-SYSTEM-GRAPHICS-PLAN。
- Quest 2 同版参考已进一步补全：完整 v77 vendor 只读提取出906个普通文件（269359659字节），新增70项显示服务/SDM/allocator/配置/固件哈希清单，连同原18库复核通过。提取内容和原始反汇编留在out；只提交工具、清单和说明。见 [QUEST2-VENDOR-REFERENCE.md](QUEST2-VENDOR-REFERENCE.md)。旧ION、QTI mapper3及SDE私有接口仍需适配，A650固件仅离线参考，不替换Surface A680.1固件。
- 闭源 GLES 可见窗口已实测：私有namespace内app_process + SurfaceControl/JNI直接通过系统EGL生成640×360缓冲区，现有Mesa SurfaceFlinger进行CLIENT合成。两次各300帧；第二次同步首帧后截图的230400像素全部精确匹配，引用/映射归零，boot/框架PID保持。首图截早了，不计画面通过。**这不是SurfaceFlinger本身的闭源替换。** 正常启动和默认Mesa哈希未变。
- 内部RGBA纹理问题范围已收窄：640×360先画内部纹理、再由GPU采样到线性AHB的双读回通过；640×360 PBO读回通过，但264×136 PBO失败。不能继续把直接client-memory glReadPixels的失败直接等同于内部纹理损坏。新只读诊断核对原库Build ID/指令及实际context后，默认和两键零配置均读到字节阈值FFFFFFFF、比例阈值0；配置尚未解决失败。具体证据见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。下一步定位CPU转换描述/路径策略，随后覆盖其他合成格式、SF自身、Vulkan配套加载与持久化。

- 闭源 GLES 多平面新进展：mapper3 按真实 mapper5 PlaneLayouts 解析 YV12/NV12，返回真实 Y/Cb/Cr 地址和步长；格式35仅在证实NV12布局后翻译为QTI具体格式0x109。两格式640×360各30帧，264×136、1280×720、1920×1080、2880×1920各3帧，均经外部YUV纹理→线性RGBA AHB→glReadPixels及CPU读回通过，4×4变色网格最大误差1，释放回零，mapper4哈希不变。**新的整套替换阻断点：原库内部RGBA纹理作为输出时，640×360纯RGB/YUV对照均有损坏；640×384及线性AHB输出通过。** 不能将成功的线性路径等同于内部纹理修复。ESX私有Direct候选配置没有改善此失败；并非已证实GMEM根因。下一步优先查内部纹理的布局/压缩/对齐，随后接可见窗口与SF。lockYCbCr适配已编译但该回调未在此矩阵中独立触发验证。详见 QUEST2-GPU-RESEARCH「YUV多平面与内部纹理阻断点」。

- Quest 2 原配 GLES 的 ANativeWindow/BufferQueue 路径已推进：参考 v77 eglSubDriverAndroid 的 flags bit3 判断，mapper3 对已验证的线性 RGBA fd-backed 缓冲区提供该兼容位；新增窗口代理按真实 dequeue/queue/cancel 与 donor 引用释放映射。Android 系统加载器下 legacy/core 两入口各100轮、1200帧精确读回通过，四个额外尺寸至2880×1920各12帧通过；legacy每轮映射/窗口引用均回到零。私有链接的共享图像/错误导入150轮回归通过。系统未公布 EGL_EXT_platform_base，EXT测试明确返回77（不支持，不计通过）。正常 Mesa/boot ID/框架 PID 保持。下一步仍是多平面视频、可见窗口和 SurfaceFlinger；ImageReader BufferQueue 不是桌面呈现证明。详见 QUEST2-GPU-RESEARCH 的「原配窗口导入与 BufferQueue 生命周期」。

- 最新闭源 GLES 已通过 Android 系统加载器：独立 mount namespace 中用系统 libEGL/libGLESv2 加载 v77 包装/桥接/原配库，无 LD_PRELOAD/LD_LIBRARY_PATH。EGL 1.5 core 150 轮、五尺寸线性 RGBA 双重读回与普通离屏路径通过，默认桌面 Mesa、boot ID/框架 PID 均不变。core/KHR 四种私有入口的共享引用与错误导入释放也各150轮通过。下一步是 ANativeWindow 交换、视频多平面、合成器和持久化，完整系统替换仍未完成。详见 QUEST2-GPU-RESEARCH「EGL 1.5 与 Android 系统加载器接入」；以下阶段记录保留历史证据，旧的 core/系统加载未覆盖已由本条更新。

- EGL 包装模块已新增：KHR 原生图像导入的导出符号和 getProcAddress 两入口统一代理；未启用探针手工代理的两组各150次循环实测通过，最终映射零残留。仍是私有 preload，EGL1.5 core、窗口交换与系统加载尚未覆盖，不是已完成系统替换。详见 QUEST2-GPU-RESEARCH。

- GLES 引用生命周期新进展：私有 native-buffer 代理已按 donor inc/dec 回收实际映射；早销毁 EGLImage 后纹理继续绘制，以及两个 EGLImage 共享同一 AHB 的 150 轮测试均通过。引用 2→1→0 和最终零残留各150次，框架未重启。代理仍仅接在私有探针，必须接入所有 EGL 导入/窗口路径后才能用于系统，不能混合代理与非代理使用来释放共享映射。详见 QUEST2-GPU-RESEARCH 最新章节。

- GLES 生命周期继续推进：已提供显式的完整 EGL 拆除后地址映射释放钩子；同一进程连续 150 次创建/三帧双重读回/成功拆除/释放，450 帧通过，150 个映射均释放，框架 PID 未变。钩子仅由私有探针在全部资源销毁后调用，不是单图像引用计数或已部署的系统包装层，不能据此接管桌面。详见 QUEST2-GPU-RESEARCH 后续记录。

- Vulkan Direct 完整 Wild Life 首轮完成：5881 / 35.22 FPS，12.8/40.6 秒两张采样正常，先前横带和粉色噪点未见；峰值 58.0°C。真实闭源映射和应用候选哈希已核对，结束已恢复 Mesa，boot/框架 PID 未变。未复跑或逐帧验证，不能宣称完整系统替换；详见 QUEST2-VULKAN-DIRECT 最新结果。

- 闭源 GLES 共享图像已推进：修复 QTI 错读 minigbm handle 的 CPU 基址、mapper3 lock/unlock 保留导入生命周期、物理高度语义后，同一 v77 配套库在 64×64、13×7、257×129、1920×1280、2880×1920 的 shader→glReadPixels→AHB CPU 对照均三帧通过。原 mapper4 逐字未变，正常系统仍是 Mesa。映射缓存仍为进程级有界实验实现，窗口生命周期、多平面和系统合成未完成，不能部署成默认桌面。详见 QUEST2-GPU-RESEARCH 最新章节。
- 闭源 Vulkan 已按反汇编确认的 RenderMode=Direct 生成独立路径候选；实际配置读取和基础计算/图像/三角形/AHB 读回通过，尚未重新跑 Wild Life，不能称为花屏修复。详见 QUEST2-VULKAN-DIRECT.md。

- Quest 2 原配参考已补全：v77 的 18 个 EGL/GLES/Vulkan、mapper/gralloc/metadata 库均固定哈希，新增 vendor 范围重建和离线审计脚本。原配布局函数与实际 minigbm 五尺寸对照通过，64×64 所需长度与实际分配相同，暂不支持空间不足解释；未证明地址/缓存/同步正确。另一个混合 v819 GSL 对照仅通过离屏和 import-only，不能称为原厂完整栈或 AHB 绘制通过。正常镜像仍是 Mesa，详见 QUEST2-GPU-RESEARCH 后续章节。

- 完整 Quest 2 v77 EGL/GLES donor 已取得并固定哈希；闭源 GLES 的 pbuffer/FBO 三帧像素读回通过。mapper3/ION 控制 FD 私有适配后，AHB 导入仍失败并出现 fence 超时、GPU hang，用户确认黑屏后手动重启。重启后自动 SSH 恢复、boot_completed=1、Mesa 哈希和 screen-only guard 正常；pstore 为空。未写启动镜像。不要原样重复 AHB 失败路径或部署桌面，详见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。

- 闭源 v819.1 Wild Life 已实测：完成 6357 / 38.07 FPS，但前中后段均严重花屏，渲染正确性失败；该成绩不能与有效 Mesa 成绩比较。临时选择器已撤回，原 Mesa 哈希和完整读回复验通过。证据与复现见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。

- Quest 2 闭源 GPU 研究在 `codex/quest2-proprietary-gpu`：v819.1 经 profile 规避和进程内 QTI→mapper5 桥接，计算/图像/三角形、多尺寸线性 RGBA AHB 读回及普通 APK 全分辨率呈现已通过；v690 依赖本机不存在的 KGSL。范围与复现见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。桥接不支持通用格式，不能作为已完成的系统替换；当前桌面驱动、镜像及频率未改变，闭源 APK/库留在忽略目录。
- 2026-10-28：麦克风已完成并通过重启实测，详见 [MICROPHONE.md](MICROPHONE.md)。方向传感器、前后摄像头按用户要求暂缓，明确缺口记入 [TODO.md](TODO.md)。Wild Life 花屏已通过 A680.1 + 3DMark 专用 sysmem 兼容补丁缓解并设为默认；重启回归 5264 分 / 31.52 FPS，前中后段采样正常。GMEM 根因、更多应用与长期功耗仍待查，详见 [WILD-LIFE.md](WILD-LIFE.md)。
- 真正休眠修复暂停；用户改为要求“只关闭屏幕”，具体保护策略见下节。
- 音频修复已于 2026-10-27 恢复推进：停播/重新开声爆音、音量小、缺低频及声像问题仍开放，见 configs/audio-known-issues.json。
- 蓝牙其他配置文件、LTE 有 SIM 联网及长期稳定性尚未完成验证。用户已要求推进 Turnip Vulkan，当前实测与后续范围见图形章节。

## 关于设备信息

### 资源监控器温度和电池读数（2026-10-28）

用户报告 DiagnosticsPrebuilt 的概览数据不准。实机截图为 CPU 0°C、电池容量 0 mAh、健康度 0%，循环次数 620。内核 CPU TSENS 为 43–44°C；Android 默认 thermal_info_config.json 的 Sensors 数组为空，thermalservice 返回无传感器。BAT1 提供 energy_full_design=38.2 Wh、energy_full≈28.71–28.73 Wh、7.58 V 标称电压，但没有 Android healthd 所需的 charge_full/charge_now 等接口；充电状态、电量、电压和循环次数原本有效。

已新增 SQ2 Thermal HAL 配置：以八个 CPU 核心传感器的最大值作为 CPU 温度，统一毫摄氏度→摄氏度，GPU 单独发布；不添加 HAL 降频控制，保留既有内核温控。独立 sq2_battery_compat 模块为现有 BAT1 增加只读转换属性，保留原驱动、原事件及充电策略。设计容量约 5039 mAh，当前满充约 3790 mAh，健康度 75%；mAh 为能量除以标称电压的估算值。该界面的“电池容量”显示设计容量，当前电流的中文标签为“当前电量”，单位 mA。healthd 的故障健康状态仍 UNKNOWN；没有电池温度、制造日期等硬件信息，不能把缺失值说成真实 0°C，也没有伪造 GOOD 状态。

在线拔下充电器后，用户确认切换到电池供电；内核、Health HAL 和 UI 同步显示 Discharging、AC=0、约 −0.97～−1.06 A，截图为 −1068 mA。重新接上后恢复 Full、AC=1、0 mA，满电时零电流正常。CPU UI 显示 43.6–44.4°C 并随负载更新。制造商容量估计会略随固件更新变化，28.71→28.73 Wh 属于本次实读变化。

通过一次性启动项完成重启：资源助手自动 READY，模块哈希 d5a75996…，Android HAL CPU 读数约 52.9°C、GPU 49°C，电池容量和 75% 健康度自动恢复，用户确认重启后显示正常。CPU 仍为 schedutil、上限 1.7664/3.1488 GHz，CPU/GPU cooling state=0、仅关屏 guard=Y。测试包已设为默认，initramfs d519b958…；Image/DTB 保持 CPU 适配版本，临时菜单移除，旧镜像 e16a7a01… 留在本地 out/cpu-frequency。设为默认后未再次重启。sq2.monitor=0 可跳过此适配，救援入口不执行它。

持久化只改变 /init、新增助手、温度 JSON 与兼容模块；逐条审计确认音频、触摸、图形和凭据内容保持原样。构建缺原始 Module.symvers，8 个未定义符号都已对照运行内核导出并实机加载，不声称完整 modpost 校验。原始截图/日志保存在忽略的 out/resource-monitor；构建流程见 BUILD。

SettingsGoogle 的“型号”读取 Build.MODEL；CPU Info 由 DiagnosticsPrebuilt 的 HardwareInfoProvider 提供，其 DefaultCpuDataSource 读取 Build.SOC_MODEL。原镜像显示 XPS Googlebook，CPU 为 unknown。apply-device-info 在原 Android init 加载属性之前，为现有 build.prop 创建 RAM 文件绑定覆盖：型号 Surface Pro X、品牌/制造商 Microsoft、ro.soc.model=Microsoft SQ2。保留用户设置的设备名称、mica 产品代号、android-desktop 硬件分派、ABI、构建指纹及软件版本，不修改只读 EROFS 镜像。

内核版本页通过 Os.uname() 和 DeviceInfoUtils 正则解析；原构建日期“2026-10-27 00:00:00 UTC”缺少星期，解析失败。build-sq2-screen-only.sh 改为“Tue Oct 27 00:00:00 UTC 2026”，内核 release 保持 6.3.8-sq2-android1、配置及模块 ABI 不变。新 Image 已通过 QEMU 只关屏保护测试。实机正常重启后，设置页面截图确认 CPU 为 Microsoft SQ2（8 个线程）、型号 Surface Pro X，Android 版本子页显示 6.3.8-sq2-android1 和 #17 Tue Oct 27 00:00:00 UTC 2026。用户原有设备名称保留。

## 启动结构

- Surface Pro X SQ2、16 GB，已替换为实验用 512 GB NVMe；内核 6.3.8-sq2-android1，内置 NVMe 已启用。
- ESP 分区 6，userdata 分区 1；布局见 scripts/build-googlebook-internal-ssd.py。super 切片映射为只读 EROFS，适配经 RAM overlay 注入。
- ESP /EFI/BOOT/grub.cfg；内核/DTB/initramfs 在 /boot/sq2-android1/。菜单现在为正常音频、独立救援、关机，正常默认、3 秒；原无音频第 2 项已删除。
- 正常 initramfs 准备必要驱动、固件与 Android 挂载，不执行测试网络连接；用户随后要求保留自动 SSH，现由 ssh-framework-boot 等待 Android boot_completed 后后台启动 Dropbear。SSH 根目录仍是调试 initramfs。
- Android 命令：nsenter -t 1 -m -r -- /system/bin/<command>；Android 文件树 /proc/1/root；ESP /media/esp；本次日志 /logs。
- sq2.audio=1 启用音频。如需临时隔离音频，可在 GRUB 编辑正常入口去掉该参数，不再提供单独菜单项。

### 正常启动去除调试等待

正常启动的内核准备阶段不执行 network-up、start-ssh、keep-debug-route、wifi-framework-boot/rollback、memfd 自测、boot-kmsg 或 native-monitor。不再等待实验室 SSID 关联和 DHCP，也不强制打开 Wi-Fi 或覆盖已保存网络。helper 输出只写 initramfs RAM，避免同步写 ESP；必要的 QCOM QRTR/PD mapper/RMTFS 固件服务、ath10k、音频/输入/GPU/RTC/FUSE 等仍保留。触控早期初始化并行执行，boot_completed 后的触控恢复仍生效。

sq2.rescue=1 自动启用 debug，保留独立救援联网和 SSH；sq2.debug=1 可临时恢复完整 Android 调试启动。QEMU smoke 模式仍保留验证日志。用户随后要求保留自动 SSH：正常模式的 ssh-framework-boot 在 boot_completed 后启动公钥认证服务，使用 Android netd 自身路由。实机无需额外路由即可连接，故不维护诊断子网规则，也不打开 Wi-Fi、不关联 SSID、不运行 DHCP。sq2.ssh=0 禁用该助手。显式 debug 模式继续走旧调试路径，不重复启动 SSH 助手。最终正常入口重启后，已通过原公钥自动连接；启动参数没有 sq2.debug=1，且没有 network-up/monitor 日志。

此前禁用自动 SSH 的精简入口重启后，用户确认桌面、Wi-Fi、触摸和键盘正常；当时 TCP 22 不接受连接。未进行严格启动耗时基准测试，不宣称具体节省秒数。

## 图形与框架稳定性

最新默认部署为 Wild Life 兼容版 initramfs `fa64305d…`、Turnip `0b36eeba…`；由已验证的麦克风基线 `9dd3ff7a…` 仅替换 Vulkan HAL。一次性重启后自动加载，计算/图像/AHB 与完整 Wild Life 回归通过，随后提升同一镜像为默认并恢复原菜单，未再次重启。Image/DTB、麦克风 HAL `774cd7e7…`、IPTSd、CPU/GPU 温控保持。下文早期版本哈希为历史记录，当前完整哈希以 configs/deployment-status.json 为准。

scripts/patch-mesa-sq2-multiplane.py 修复 mapper5 辅助实例未导入 handle 导致元数据查询失败，以及单平面假设。RAII import/free，读取 FOURCC、modifier、plane layout，支持 1–4 平面并检查偏移/步长，返回原 handle 借用 FD，避免释放后 FD 失效。

此前 YV12 640×360 导入失败触发 SurfaceFlinger GaneshBackendTexture / SkiaRenderEngine::mapExternalTextureBuffer abort；后续 AudioService watchdog 为该轮级联现象，不应把全部重启归因于音频。新 RGBA、YV12/格式 35 GLES 读回通过。无音频视频观察 825 秒、音频开启观察 503 秒，框架 PID 未变化且用户确认有声；这是有限测试，不是长期保证。

保留 vendor.minigbm.debug=nocompression，解决早期文字/动画彩色损坏。源码 probe/graphics/；GLES 集成 build-sq2-mesa.sh、build-googlebook-graphics-overlay.py、pack-sq2-stability.sh。

### 2026-10-28 Turnip Vulkan

本机 WSL Ubuntu 24.04 使用固定 Mesa 26.2.4、NDK r28c、MSM 后端构建独立 Turnip HAL，沿用 mapper5 import/multiplane 补丁。驱动 SHA256 为 10b9e7f540311b8148cc88fe1f5c80531a680431539593ce3cd3663d336d0225；构建得到的 libdrm 与正在使用的版本完全相同，无需替换 GLES 或 libdrm。不要使用面向手机 KGSL 的预编译驱动。

硬件商品型号是 **Microsoft SQ2 / Adreno 690**，微软规格确认：https://news.microsoft.com/es-xl/surface-pro-x-fact-sheet/ 。当前设备树 compatible 为 qcom,adreno-680.1，Mesa 走 GPUId(680) 路径并显示 Turnip Adreno (TM) 680；测试日志 A680 指驱动识别路径，不是把 SQ2 硬件改称 680。用户指出 SQ2 为提频变体、最高 661 MHz；本次实机 devfreq 可用频率为 177/256/315/405/461/500/514 MHz，max_freq=514 MHz。661 MHz 尚未启用/验证，需进一步核对 SQ2 对应 OPP、电压及 GMU/时钟配置，不能只改名称或盲目拉高频率。

基线 ro.hardware.vulkan=none，系统加载器最初无物理设备。在线将 HAL 放入 RAM vendor overlay 的 lib64/hw/vulkan.none.so，按现有属性实现临时加载。系统 libvulkan 报告 Vulkan 1.3.354。直接 HAL 和系统加载器均通过 16×256 字计算校验及 64×64 optimal-tiled 图像清除/拷贝/逐像素读回；系统加载器额外通过 AHardwareBuffer 分配、mapper5 导入、GPU 清除、CPU 锁定后 4096 像素校验。

普通应用 org.surface.sq2.vulkanprobe（显示名 SQ2 Vulkan Test）通过系统加载器建立 2880×1920、4 图像交换链，两次各提交 120 帧，用户确认深蓝背景彩色三角形可见。此前黑屏源于锁屏/窗口隐藏，解锁后通过；不归因为 GPU 故障。本轮 SurfaceFlinger/system_server PID 未变，检查的内核日志无 GPU fault/hang。此为有限功能验证，不等于 Vulkan CTS 合规、游戏兼容性或性能保证；未切换桌面的 skiagl 渲染器。

scripts/pack-sq2-vulkan.py 基于当前 aa5d60f4… 镜像，仅增加 Vulkan HAL、修改 ro.hardware.vulkan=freedreno，并用 Vulkan 1.3 feature 替代 donor 的 1.4 宣称、移除未经本端验证的 dEQP 日期宣称。最后条目审计仅允许这五个路径变更；密码 SSH addon 保持最后，触控、音频 HAL/启动助手和 /init 均保持原字节。回滚基线已在忽略的 out/touch-heatmap/native-heatmap.cpio.gz 校验；新镜像 out/sq2-turnip/native-turnip.cpio.gz 哈希为 5a9c9df13e6e2d4116e0b8d4c532c14c073aebc5389f6c9266317d2ab9cd100d。最终部署结果见 configs/deployment-status.json。重启验证尚未完成，当前运行只读属性与 PackageManager 的旧 1.4 宣称要等新镜像正常启动后重新核对。

上述版本的启动验证及频率后续见下一节；后续仍需实际应用兼容性与持续负载验证。

### 2026-10-28 GPU 661 MHz 与温度支持

用户要求参考 8cx Gen 3 完成 SQ2 GPU 频率更新。对照固定 linux-surface b86f786… 与 Linux v6.12 的 SC8280XP 设备树，确认两者分别为 A680.1/SC8180X 和 A690.0/SC8280XP 路径，不能因商品名同为 690 就替换硬件身份、固件或整套电压表。SC8280XP 的 655 MHz/TURBO、690 MHz/TURBO_L1 仅作参考；本端新增 590/661 MHz 均保持原 514 MHz 已使用的 TURBO_L1=416，由本机 command DB/GMU 选择电压，未编造毫伏值。此配对是经有限实机验证的实验配置，尚不是提取出的 OEM 曲线。

`build-sq2-gpu-opp.py` 基于实际部署 DTB 7a4d1c73… 构造增量，原低频表及非目标属性保持。590 MHz 经一次性 GRUB 入口启动并通过 30 秒校验；首次发现无温度节点，未将该轮称为温度验证。检查发现原配置未启用 TSENS（受未启用 QFPROM 的 Kconfig 依赖影响）。重新取得固定内核源码，配置 olddefconfig 前后完全一致，仅构建 qcom_tsens.ko；52 个未定义符号均在目标内核导出表，vermagic 匹配后实机加载成功。模块 SHA256 dd85fbf8…，源码构建缺少原 Module.symvers，不能称为全量 modpost 校验。GPU-top 读数可用，GPU-bottom 未注册，本次不宣称所有热区完整支持。

`pack-sq2-tsens.py` 只增加匹配 TSENS 模块与 /init 的早期加载钩子，在 Android donor 模块加载之前接管。661 MHz DTB 额外添加 GPU-top 75°C passive trip、5°C hysteresis，绑定现有 devfreq cooling；TSENS 加载失败的 661 MHz 启动被阻止，原 DTB 可回退。最终正常启动使用 `sq2-gpu-661-thermal.dtb`（09b6d3a8…），救援仍用原 `sc8180x-surface-pro-x.dtb`（7a4d1c73…）。Image 保持 e7c7a3e5…，内核 release 6.3.8-sq2-android1 及其他模块 ABI 不变；这是内核侧设备树与模块更新，不是替换整个 Image 或升级内核版本。

重启还发现先前 Vulkan delta 缺少 permissions 父目录：文件虽在 cpio 审计中，内核并未成功解包。`pack-sq2-vulkan.py` 现明确写入缺失父目录并增加回归测试。修正与 TSENS 合并后的 initramfs 为 f8fb83358edeead8ac10c53e20b53b06b9e1597ab8268ab345a262a744691f50，密码 SSH addon 保持最后；仅指定图形/热管理路径变化，音频实验未混入，IPTSd 哈希仍为 e4610813…。

最终 661 MHz 组合已经从一次性实验入口重启，随后以相同启动参数提升为默认正常入口，未在提升默认后再重启一次。实机 ro.hardware.vulkan=freedreno，HAL 哈希匹配；PackageManager feature 版本为 4206592（1.3），旧 dEQP 日期宣称已消失。用户确认“已解锁联网，桌面/触摸/视频均正常”，WSL 公钥 SSH 可用，guard=Y。两个测试期间框架 PID 各自保持，检查日志未发现 GPU/GMU fault、hang 或 timeout。

带温度记录的 590 MHz 测试：30 秒，28462 轮，每轮 4096 字、每字 4096 次整数递推，逐字校验通过，GPU-top 41.3–44.4°C。661 MHz 测试：同样 30 秒，26626 轮，43.2–45.9°C；计算、图像及 AHardwareBuffer 读回均通过。轮数受并发应用/CPU 等影响，不是受控性能对比，也不宣称提频性能收益。devfreq 显示请求频率；当前 a6xx_gmu_get_freq 返回缓存值，不能把它称为实测 PLL。

降频规则通过温度模拟验证：模拟 76°C 时 cooling state 从 0 到 1，撤销模拟后回到 0；没有通过实际加热达到高温。结束时 min_freq=177 MHz、max_freq=661 MHz，空闲 cur_freq=177 MHz，模拟温度已撤销。长时间游戏、不同供电条件、实际高温及电池续航仍待验证。

回滚：把正常入口 devicetree 改回原 `sc8180x-surface-pro-x.dtb` 即回到 514 MHz；原救援入口直接使用该 DTB。旧 grub 和 DTB、两轮测试及镜像审计保存在忽略的 out/gpu-opp/，上一版本 initramfs 位于 out/sq2-turnip/native-turnip.cpio.gz。本轮未删除历史备份，ESP 写入均经过盘型号/UUID/标记确认、哈希/gzip、sync 和同卷原子替换。

### CPU 调频适配（2026-10-28，后续已完成）

用户询问 CPU 当前速度和最高频率。发现 qcom-cpufreq-hw.ko 已在 initramfs，但没有加载，/sys/devices/system/cpu/cpufreq 下无 policy。按实际 DTB 和匹配驱动定义，只读读取 0x18323000/0x18325800 的频率 LUT 与请求索引：小核域最高 1.7664 GHz，当前请求 index 12 = 1.6704 GHz；大核域最高 3.1488 GHz，当前请求 index 0 = 825.6 MHz。此为当时请求档位，不是实际周期计数，也不能证明 CPU 自动随负载升至最高频率。未加载 CPU 调频模块或改写 CPU 寄存器；接通调频驱动及负载验证作为独立后续项，不能把本轮 GPU 更新表述为 CPU 性能也已完善。

随后用户明确要求适配 CPU 频率。加载现有匹配模块后建立 policy0（CPU0–3）和 policy4（CPU4–7），但大核只开放到 2995.2 MHz；3072/3148.8 MHz 报 Voltage update failed。核对驱动发现是静态 DT OPP 缺项导致 dev_pm_opp_adjust_voltage 无法更新，并非已证明硬件电压不足。新增这两档并沿用原最高档 interconnect vote，实际电压继续读取本机硬件 LUT；不写入推测电压。

启动前由 cpu-frequency-start 加载模块，默认 schedutil，小核 300000–1766400 kHz、大核 825600–3148800 kHz。新 DTB 同时保留 GPU 661 MHz/75°C 温控，为两个 CPU cluster 新增 80°C 被动降频及 5°C 回差。镜像审计只改变 /init 和新增助手，音频、触摸、图形内容逐字未变；密码 SSH addon 保持最后。

一次性 GRUB 启动验证后，八个绑核进程各运行 20 秒确定性整数递推并全部校验通过，采样显示小核 1.7664 GHz、大核 3.1488 GHz，CPU 最高采样温度 53.3°C。负载结束后观察到较低频率，仍使用 schedutil。两集群分别模拟 81°C，cooling state 0→1，小核上限降至 1670.4 MHz、大核降至 3072 MHz；移除模拟后状态恢复 0、频率上限恢复。未诱发真实过热。用户确认重启后桌面、触摸、视频正常；WSL 公钥 SSH 恢复，框架 PID 在校验期间保持不变。

已将相同测试包设为默认：DTB sq2-cpu-gpu.dtb（a5a25e43…），initramfs e16a7a01…。Image/ABI 不变；原 514 MHz DTB 救援入口跳过 CPU 助手，普通入口也可通过 sq2.cpufreq=0 跳过。一次性测试菜单已移除，提为默认后未再次重启。旧镜像 f8fb8335… 与 GRUB 保留于本地忽略的 out/gpu-opp、out/cpu-frequency；ESP 同卷原子替换后空闲约 221 MiB。此为驱动请求频率与短时正确性验证，不是独立核心时钟测量、长期稳定性或性能/续航基准。

## 触摸及输入

IPTSd 3.1.1 加 scripts/patch-iptsd-sq2-touch.py 状态/连续性修复。configs/iptsd-sq2.conf 保留 Contacts AspectMax=2.0，触点检测与稳定性阈值默认；用户确认轻点、连续拖动、双指缩放正常且无自动点击。DFT ButtonMinMag=4000 是单独的笔配置。

2026-10-27 后续复现左上角幽灵点击：用户确认未操作的 30 秒采集出现 4 次短促点击，仅 IPTSd 虚拟触摸屏产生事件。临时带原始报告/触点状态记录的守护进程又采得 3 次；ARM 离线重放准确复现其帧号。原始 46×68 热图在 (row,col)=(0,0)、(0,67)、(1,0)、(2,0) 有近饱和尖点，周围未触摸格通常为约 180。高斯拟合偶尔将它们变成 valid/stable 触点。尚不能确定异常来自传感器、固件还是 SPI 传输，不应直接宣称硬件损坏。

两帧起触确认虽能消除第一份录制的点击，在线后仍抓到 2 次，已撤掉该实验。最终 scripts/patch-iptsd-sq2-heatmap.py 只针对 045E:0C21 触摸屏及 46×68 热图，在拟合之前修正上述四格：仅当归一化强度≥239/255 且高于原始邻居中位数超过 40/255 时，用邻居中位数插值。不裁掉屏幕区域，不提高接触检测阈值，不增加起触或抬起等待；其他采样格和硬件不变。

最终版将第一份 3610 帧静置录制的 3 次按下降为 0；同一份真实触摸录制中的左上角/中部输入仍能上报，但不能仅由按下次数证明手势质量。随后用户确认最终在线版本“手势正常，未见自动点击”。两轮在线采集中未再见原来的左上角点击，含其他位置的操作，不能称为两轮完全零输入。IPTSd 连续性及热图夹具测试通过。有限观察不能代替长时间静置、充电/电池条件和重启后的回归。原始录制、临时诊断程序、带凭据的镜像及备份仅放在忽略的 private/out。

本次用本机 WSL Ubuntu 24.04 构建，最终 IPTSd 为 e4610813…；已在 RAM 中运行，并写入当前 ESP 启动镜像 aa5d60f4…，完整哈希见 deployment-status.json。替换前镜像 4ea548ba… 已复制到本机 out/touch-heatmap/native-before-heatmap.cpio.gz 并校验；新镜像 gzip 校验及最后条目审计通过，实际只更换 bin/iptsd，保留新加入的操作者公钥，密码 SSH addon 仍放最后。sync 后同 ESP 原子替换，未重启，boot ID 不变且只关屏 guard=Y。下次自然重启后仍需核对有效二进制及实机手势，不把“写入镜像”表述为“重启验证通过”。

随后按用户要求从正常入口完成整机重启：boot ID 已变化，boot_completed=1，IPTSd 哈希仍为 e4610813…，仅关屏 guard=Y，nohibernate 保留。用户确认未操作的重启后 30 秒采集中，触摸屏及其他被采集输入设备均无事件（timeout 的 Terminated 行不是输入事件）；随后用户确认左上角轻点、连续拖动、双指缩放均正常，也未见自动点击。至此启动持久性及本次有限实机回归通过，长期静置与不同供电条件仍待观察。

本次重启后还分别验证 Windows 与 WSL 操作者密钥：显式禁用 password、keyboard-interactive、agent 回退，仅 publickey 认证，严格校验原主机身份，两者均取得 root。WSL NAT 当时无法直连设备，使用本机 Windows TCP 字节转发后，由 WSL 原生 OpenSSH 和 WSL 自己的私钥完成认证；这不是 WSL 直连网络通过。转发脚本和可复用连接配置在忽略的 private/sq2-ssh/，身份、地址、原始输出不入 Git。

输入节点发布时序/权限修复使 EventHub 接入设备。正常开机可用与休眠后恢复是独立结论，后者未解决。

只关屏内核首次启动后曾出现触摸完全无响应：SPI ready、resume_count=0、IPTSd 存活且 Android 已枚举设备，但连续操作采集无事件。保留同一二进制/配置重启 IPTSd 后用户确认恢复。新增 touch-framework-ready，在 boot_completed 后延迟 5 秒重新初始化一次；这是启动阶段的恢复措施，具体丢失原始触摸模式的原因尚未证实，不归因为真实系统休眠。

后续重启已执行 SQ2_TOUCH_FRAMEWORK_REINITIALIZED，用户确认触摸正常。键盘盖热插拔另有独立权限竞态：内核已创建 09AE/09AF/09B0 输入设备，但 EventHub 先于 ueventd 设置权限打开节点并报 Permission denied，此后没有重试。cover-input-watch 只匹配这些 SSAM 键盘盖节点，按 sysfs input 身份识别每次重新枚举，以 root:input 0660 权限重新发布 IN_CREATE；不能仅缓存 eventN，因为热插拔会复用编号。

用户再次拔插后确认键盘、触控板均能自动恢复。随后电源键关屏约 20 秒再亮屏、合盖等待后开盖，用户确认显示、触摸与键盘都正常。

## Wi-Fi 与蓝牙

Wi-Fi 使用内核接口及 Android 通用 HAL 适配：sq2-wifi-driver.c、sq2-wifi-hal.cpp。正常启动由 Android 自身恢复已保存网络；wifi-framework-boot 仅在显式 debug 启动用于调试连接及回退。保留实验室联网引导跳过逻辑。

蓝牙由 Linux hci_uart/btqca 初始化 WCN3998，sq2-hci-bridge 将真实 HCI_USER socket 桥接到 /dev/sq2-bt-h4，Cuttlefish AIDL HAL 接管。patch-sq2-bluetooth.py 包含关闭 serdev 防护、特定 baud ACK 处理及诊断；bluetooth-framework-boot 和 configs/sq2-bluetooth.rc 持久化。用户确认名称显示、配对成功；A2DP、通话等全流程未验证。不要在线反复卸载 QCA transport 模块。

后续启动曾在过早的 QCA 固件下载阶段出现异常回复，HCI_USER bind 返回 EBUSY，桥接退出且 Android 卡在 BLE_TURNING_ON。bluetooth-framework-boot 现等待 sys.boot_completed=1 后再延迟 8 秒启动控制器；桥接对 EBUSY/EAGAIN 最多等待 120 秒，其他错误仍失败。新流程只发布 HAL transport ready，不写 bluetooth_on，不调用 enable/disable；由 Android BluetoothManager 保存并恢复用户开关状态及飞行模式策略。开机加载控制器/注册 HAL 不等于替用户打开蓝牙开关。

已完成两次重启验证：保存值 1 时自动恢复 ON；保存值 0 时，控制器和 HAL 就绪后仍为 OFF，BluetoothManager 显示 never enabled。随后手动 enable 在约 0.85 秒转为 ON，恢复用户原来的开启状态；观察到的框架蓝牙崩溃数为 0。重复运行启动助手保持桥接 PID 与 OFF 保存值不变，不重新关闭已接管的 HCI 控制器。此验证覆盖开关/保存状态，不代表所有蓝牙配置文件已回归。

## 音频

### 当前有效版本：麦克风接入（2026-10-28）

WCD9340 双 DMIC → MultiMedia2 / CARD_0_DEV_1 已打通；策略发布 48 kHz S16 stereo 内置麦克风。通用 HAL 的 XML 转换会把该地址覆盖为 bottom，导致选错 card0/dev0；新增严格限定原始 donor 哈希的单指令补丁，仅跳过 IN_MICROPHONE 的地址覆盖。实际播放/录音传输、pacing、MonoPipe 保持 stock，勿叠加旧实验 patch-sq2-audio-hal.py。

当前实际 HAL 为 774cd7e7…，音频启动助手显式设置可执行标签、重启并检查 /proc/PID/exe，然后刷新 audioserver，消除了“路径已替换、运行仍是旧 inode”的竞态。用户确认 48 kHz 双声道、16 kHz 单声道录音/回放及原诊断应用正常；一次性启动后再次确认诊断录音回放正常。路由及进程哈希在重启后核对一致。不是 AEC/波束成形或全音频认证。

已将实测包 9dd3ff7a… 提为默认，恢复原正常/救援/关机菜单；原 d519b958… 基线保留在本地 out/resource-monitor。只改策略、HAL、音频启动助手和新增麦克风路由四项，内核/DTB/触摸/GLES/Vulkan/温控/电池及凭据不变，凭据 addon 最后。以下旧音频实验记录保留作历史，不能覆盖本节当前状态。完整构建、证据边界见 [MICROPHONE.md](MICROPHONE.md)。

ADSP → SLIM/WCD9340 → SoundWire/双 WSA881x。物理左右均有声；Android AIDL HAL 和 48 kHz S16 stereo policy 已接入。patch-sq2-audio-hal.py 去掉模拟器跳帧逻辑，校验原始/修改后哈希。audio-drivers-start、audio-speaker-route、audio-framework-boot 负责固件、路由、HAL。

调音问题仍开放。历史音频期间发生过整机重启；有限视频观察不能证明所有音频故障已解决。

2026-10-27 音频实验：RX7/RX8 从 raw 66 改为 72（-12 dB），PA 增益未提高，DAC/COMP/BOOST 配对端口保持。用户仍报告视频音量小、像通话。pmdown_time=0 的直接 ALSA 单次测试无爆音，但实际视频暂停立即爆音，因此已恢复原 5000 ms，未保留该策略。不要把一次无爆音视为修复。

新增 patch-sq2-audio-sequencing.py 在 SoundWire teardown 前静音 WSA881x，并在 SPKR PGA PRE_PMD 提前静音。实验模块已重启加载，但用户报告分开播放左右 WAV 时右侧开始即爆音，尚未证实补丁有效。五档音调测试后用户澄清从 160 Hz 起听不到，左右分别表现尚待核对；这不是校准频响测量。RX INT7/8 HPF 配置为 0.48 Hz。generate-sq2-audio-probes.py 提供渐入渐出、固定幅度测试 WAV；channel-continuity 在同一个 PCM 流中左/右/左/右切换，用于区分声道异常与重新开流。sq2-audio-wav-test 可经 Android 播放与 tinyplay 完全相同的文件，避免音源幅度不同混淆结论。不得盲目增加低频/PA 增益或关闭配对 SoundWire 端口。

音频后续对照结论：同一 PCM 流中左/右/左/右均无爆音，分两次开流且间隔 10 秒时第二次开声爆音。新静音模块下将三个音频 link 的 pmdown_time 设为 0 后，直接 ALSA 两遍、同 WAV AAudio 两遍均无爆音且音量接近；用户随后确认实际视频暂停等待 10 秒再恢复两轮也无爆音。audio-framework-boot 已在 HAL 启动前设置此策略，镜像已写入，尚未验证本次策略的重启持久性或长期效果。低频/音量问题未解决。新增 test-sq2-audio-reopen.sh 为临时 A/B 工具，退出会恢复原设置，不能替代启动策略。

此次 ESP 因历史备份占满导致临时镜像写入失败，原启动镜像未受影响。已将本次回滚镜像（fc5e7a7b…）复制到工作站 out/audio-fix-20261027/native-before-audio-pmdown.cpio.gz 并校验完整哈希；只移除 ESP 上该重复备份及本次失败的 .new 文件后完成原子替换。早期 ESP 备份仍保留。下一次更新应先检查可用空间。

### 2026-10-28 播放速率与均衡器 UI

用户报告连续播放破音，随后明确指出播放明显加速。撤回所有临时均衡及音量补偿后仍复现。旧跳过模拟器 pacing 的 HAL（99e2a625…）下，13.5 秒 AAudio WAV 整次调用约 7.27 秒，直接 ALSA 约 13.60 秒；发现 MonoPipe 非阻塞写入可能丢帧但仍确认完整帧数。增加 blocking writer 的实验 HAL（bf89ba97…）只有在核对 /proc/PID/exe 后才确认真正运行，两次同一探针耗时约 15.76/15.87 秒，包含启停开销，尚不能标为速率修复。

用户自行重启后确认“音频已恢复正常”。只读核查发现：实际运行的 HAL 是原版 fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279，而 /etc 中及 APEX 挂载路径上的文件仍是 99e2a625…；启动时服务提前启动旧 inode 的竞态意味着路径哈希不能代表运行版本。该次正常播放观察中 AudioFlinger 为 48 kHz，timestamp rate 约 1，localSR 约 48001，4096 帧写入耗时约 85 ms。RX7/8 均为 72，无均衡效果链。ESP 镜像仍为 aa5d60f4…；本次未写镜像、未重启 HAL。保留此正常运行状态。

工作区 audio-framework-boot 的 SELinux 标签/强制重启/运行哈希检查及 patch-sq2-audio-hal.py 的 blocking writer 改动仍为未部署实验。**不能把它们直接打包成已验证修复**：强制重启会改变这次实际运行的原版 HAL。后续持久化应先明确选择并复核原版 HAL，而非自动部署实验版本。

用户要求推进系统均衡器并提供 UI。已从 Equalizer314 官方 GitHub release 安装 v0.0.22-beta 至当前用户，APK SHA256 6529e478d4ddc355e15a4179b0c41b0535e38334161a8161c389fb7e30a7724e（与发布资产 digest 一致），包名 com.bearinmind.equalizer314。中文参数式界面实机截图验证。它通过系统 DynamicsProcessing 实现均衡，不替换 HAL；MusicFX 已预装但本次从全局入口启动未留下可用界面。

已保存“SQ2 原声”和“SQ2 温和低频”预设，后者见 configs/sq2-equalizer314.json：125 Hz +1 dB、160 Hz +1.5 dB、200 Hz +1 dB，Q 均为 2，前置 -4 dB。三个 bell 响应会叠加，不能把单段数值当成最终频响；这是保守的相对低频补偿，整体响度可能下降，不是提高扬声器硬件增益。仅授予通知权限，频谱、混响及实验环绕保持关闭。已确认预设在 UI 显示，首次配置保持总电源关闭；等待用户通过底部中央电源按钮开启并反馈。后续仍需验证实际 effect chain、播放速度、听感和重启恢复，不把安装 UI 等同于均衡有效。

随后用户开启并反馈“低频改善且播放正常”。只读复核 AudioFlinger 全局 session 0 存在一条 DynamicsProcessing 效果链，Enabled=y、Suspended=n，控制客户端为 Equalizer314；输出 48 kHz，timestamp rate=1.00001、localSR≈48000.4。HAL 仍为原版 fd219b10…，boot ID 未变。证明此次实际视频的均衡已挂接及速率正常，重启自动恢复仍未验证。

注意：听感确认时用户已手动编辑为五段曲线（约 229 Hz/+8.41 dB、143 Hz/+9.79 dB、89 Hz/+10.50 dB、1079 Hz/+6.03 dB、5184 Hz/+3.56 dB），前置仍 -4 dB、自动增益与 limiter 关闭；不能把此次听感归因于 configs/sq2-equalizer314.json 的最初三段预设。保留用户设置未覆盖，实读 XML 和效果链快照备份于忽略的 out/audio-bass/eq314-user-confirmed.xml、eq314-active-audioflinger.txt。已告知频段叠加可能超过前置余量，若大音量破音先降低前置增益。后续不要自动重放初始化脚本覆盖用户调音。

## 合盖、电源与休眠（暂停）

### 现行策略：只关屏

用户要求禁用有问题的 s2idle，只保留显示关屏。scripts/patch-sq2-screen-only.py 在 pm_suspend 入口、冻结进程之前返回 EOPNOTSUPP。suspend.screen_only 默认开启且启动后只读，GRUB 显式指定 suspend.screen_only=1；nohibernate 同时禁用磁盘休眠。不会触发原先导致触摸失效的系统 suspend/resume 路径；显示和设备 runtime PM 保留。

lab-keep-awake 仍持有 partial wakelock 以减少自动休眠尝试，但内核保护才是最终屏障。删除强制点亮屏幕逻辑，只把旧调试用 2147483647 毫秒超时迁移为 120000 毫秒；已有其他有限设置保持不变。电源键、合盖和超时只使显示关闭，系统仍运行，关屏耗电高于真正休眠。

QEMU 已启动实际新 Image：只读开关为 Y，freeze/mem/disk 写入均失败，没有 PM suspend entry。/sys/power/state 仍可能显示 freeze/mem；它是支持状态列表，不代表策略允许进入。不要仅凭列表认为保护失效。

实机确认 guard=Y，freeze/mem/disk 同样被拒绝；共享存储仍 MOUNTED。关屏/合盖期间 SSH 持续可用，Android 显示 Asleep，但内核 boot ID 不变且 SPI resume_count 始终为 0。用户确认电源键及合盖两组恢复测试均正常。这验证的是“只关屏”，不表示真正 s2idle 已修复。

恢复旧策略必须同时回退 Image/initramfs/GRUB，或明确在后续受控研发中更改启动策略；当前不要关闭 guard。下面保留原始故障记录。

SSAM 状态来自 01:0e:01:00:01/state。patch-sq2-lid.py 将 closed 映射 SW_LID，保留 tablet switch；加载 tabletsw、gpio_keys、pm8941-pwrkey；lab-input-posture 按真实状态桥接 mica posture。用户确认合盖关屏和电源短按关/亮屏，**开盖未唤醒**。

真实 s2idle 曾全黑、SSH 消失及触摸失效。最后一次由用户长按重启，并非系统自行重启。

旧 SPI-HID 缺少 PM 回调；PM devices 阶段出现 -108 I/O/复位循环。patch-sq2-spi-pm.py 新增 IRQ/work 暂停/恢复、保留 HID 对象、暴露 resume_count；touch-resume-watch 恢复后重启 IPTSd。devices/platform PM debug 阶段能返回且设备 ready，但真实 20 秒关屏/合盖仍失败，不能标为完成。

其他线索：USB xHCI resume 曾重置；SSAM D0 转换与 display 通知待核查；stock suspend_notification_service 缺少平台支持；lab-keep-awake 框架锁未可靠阻止实际 suspend。当前不继续测试。

## 现场检查与保存

用户已要求当前实验设备允许 root 密码 SSH，保留公钥登录。原 Dropbear 编译时禁用了密码认证，现另建带静态 libcrypt 的服务端，使用原主机密钥。2222 临时端口及正式 22 端口均通过禁用客户端密钥/agent 的密码认证，id -u 返回 0；公钥连接仍工作。shadow 与明文测试输入仅保存在忽略的 private/out，不进入仓库。启动镜像追加本地密码 SSH addon，重打包注意保留最后覆盖顺序，详见 BUILD。密码变更后若需跨重启保留，须重新保存本地 shadow 并打包；诊断根文件系统内直接 passwd 只修改当前 RAM。

### 无密码锁屏

2026-10-28 调试期间用户已自行添加 PIN，随后将自动锁定延迟调整为 30 分钟，并明确继续保留该策略用于 GPU 调试。实读 user 10 的 lock_screen_lock_after_timeout=1800000。随后将 screen_off_timeout 从 120000 延长到 1800000，避免两分钟关屏打断测试；两项均为 30 分钟，不等于完全禁用锁屏。保留 PIN，不清除凭据。临时“不锁屏”资源/启动助手实验已撤回，原 Sq2Lockscreen overlay 恢复启用；以下为先前无密码锁屏适配历史。

当前用户 10 未设置 PIN/密码，关屏亮屏直接进入桌面。镜像的 android:bool/config_disableLockscreenByDefault 为 true；单独执行 lock_settings set-disabled false 未恢复锁屏。已创建 com.android.shell:Sq2Lockscreen fabricated overlay，将该资源覆盖为 false，并对用户 0/10 启用，再显式启用当前用户锁屏。

用户确认关屏再亮屏显示锁屏且能正常解锁；SystemUI 显示 KEYGUARD 和“点击或滑动即可解锁”。未创建或清除任何凭据，只关屏内核策略不变。cmd lock_settings get-disabled 在本次在线修改后仍返回 true，与实际 SystemUI 行为不一致，不能单独作为验证依据。

复现脚本 scripts/enable-sq2-lockscreen.sh 需在 Android root 环境中手动执行一次（诊断 SSH 中通过 nsenter -t 1 -m -r -- /system/bin/sh 执行已复制到 Android 的脚本）。它不随启动运行，不持续覆盖用户设置。overlay 已记录到 userdata 的 /data/system/overlays.xml，资源文件在 /data/resource-cache；后续实机重启发现该 fabricated overlay 被丢弃。现由 lockscreen-framework-boot 在 Android 启动后恢复默认资源覆盖，不调用 set-disabled，也不改动用户的密码或锁定选择。手动启用脚本仍用于首次显式选择无密码锁屏。使用 cmd power sleep 验证时需等待默认锁定延迟（本次等待 8 秒），不能用 1–3 秒立即唤醒认定无效。最终重启后无需手动执行恢复命令，关屏 8 秒再唤醒，Keyguard showing=true、inputRestricted=true；只关屏 guard 仍为 Y。回退资源覆盖可对用户 0 和实际前台用户执行 cmd overlay disable --user USER com.android.shell:Sq2Lockscreen；如需恢复原来的无锁屏选择，使用系统设置，不自动清除凭据。

使用自己的 SSH key 并确认主机指纹；连接脚本支持 -IdentityFile 或 SQ2_SSH_KEY。

```sh
cat /proc/sys/kernel/random/boot_id
nsenter -t 1 -m -r -- /system/bin/getprop sys.boot_completed
nsenter -t 1 -m -r -- /system/bin/dumpsys mount
nsenter -t 1 -m -r -- /system/bin/df -h /data
ls -l /dev/fuse
grep fuse /proc/filesystems
```

故障同时比较 boot ID、框架 PID 和日志，不只看开机动画。存储回归需实际应用操作，不只 root 写 /data。

本地 out/ 保留镜像、备份和证据，private/ 保留身份及凭据，均不入 Git。公开仓库保留可复用测试程序，不保留原始日志。部署哈希见 configs/deployment-status.json。
