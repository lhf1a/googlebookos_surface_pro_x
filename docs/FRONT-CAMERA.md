## 2026-11-15：前后摄照片/视频格式接入完成（SQ2 Hybrid v10）

已按用户最新范围完成格式与系统相机对接，偶发问题仅登记。
完整启动 boot 27706771-47d1-4b43-866c-26d1bc275698，默认 initramfs
9245c960b207b6eeba6722814c6c170696a77eb4b02a399e58237f8953495fa3，
实际 provider 2091ff3db5bb44ff99dcb7ffeadec63a6e061bda5cbc0c3787205c30f3694491。
mapper8675c6b3…、allocator d928df42…与已通过的Hybrid缓冲实现一致，未热换图形。

Camera2的JPEG/BLOB、PRIVATE、YUV420_888共用五档尺寸：
320×240、640×480、1280×720、1920×1080及本sensor原生尺寸。
系统相机识别FHD/HD/SD，默认1080p。最终启动的六段MediaRecorder录像全部通过，
H.264视频+AAC音轨，全部解码通过；实测帧率如下（不是profile标称30fps的保证）：

| 模式 | 前摄 | 后摄 |
| --- | ---: | ---: |
| 480p | 28.6 fps | 30.0 fps |
| 720p | 18.3 fps | 10.0 fps |
| 1080p | 6.3 fps | 7.7 fps |

系统相机实际DCIM保存并完整解码：
- 后摄JPEG：20261115_225912911.jpg，4224×3136，4596481字节。
- 前摄JPEG：20261115_225951008.jpg，2592×1944，1396130字节。
- 前摄1080p：20261115_230054279.mp4，31.729秒/181帧，约5.7fps，H.264/AAC。
- 后摄1080p：20261115_230200033.mp4，35.505秒/252帧，约7.1fps，H.264/AAC。

测试结束已停止诊断及系统相机，两sensor runtime_status均suspended。
证据位于ignored out/camera-android-20261115：video-v10-matrix.txt、
video-v10/verified.json、stock-v10/verified.json、stock-v10-end.txt。
原生尺寸三流和720p/1080p+原生JPEG的v9验收适用于未变更的格式/ISP路径；
v10只追加HAL的30fps传感器时序上限，并已完成最终实际录像回归。

已知/后续：CCI首次打开或切换偶发超时，本轮不再定位；
软件ISP高清帧率低、画质/曝光待调、后摄AF未接入；
IR仍仅芯片ID通过，图像与认证未完成；声音内容未主观听验。
这些不作为本轮前后摄格式接入的新阻塞，不宣称CTS、长期稳定性或SQ1实测通过。

保留Mesa回退及ESP中的72d87b16…上一录像工作包、c9a81413…拍照工作包、
7c27053b…原图形工作包。已移除的过时YUV-only/no-HAL备份9d369e03…
删除前与本地initramfs.camera-yuv-cold.cpio.gz完整SHA一致；v9包仍在本地保留。
本机完整包含原有私有状态，仅限此实验设备，不作公共镜像分发。

## 2026-11-15：v10补齐录像帧率上限，偶发错误按用户要求只记录

v9的两路480p/720p/1080p MediaRecorder实际文件均已完成；两路720p/1080p的
宽高、时长与AAC音轨读回通过。原生前后JPEG及最大尺寸PRIVATE/YUV同样通过。
VGA前摄仍用约57fps的binned RAW模式，HAL未限制profile的30fps上限。
v10在HAL打开时用驱动报告的pixel_rate/HBLANK计算VBLANK，限制最高30fps；
RAW诊断默认模式不变。前摄VGA实测28.6fps，H264/AAC文件正常；后摄首次打开又出现
既有CCI master0 queue0/soft-reset超时，按用户要求仅记录，不展开定位。

v10 provider2091ff3db5bb44ff99dcb7ffeadec63a6e061bda5cbc0c3787205c30f3694491，
完整候选9245c960b207b6eeba6722814c6c170696a77eb4b02a399e58237f8953495fa3正在部署。
格式表、ISP、Hybrid缓冲与v9相同，接下来仅验证完整启动的录像矩阵与系统相机保存。

## 2026-11-15：v9原生JPEG、HD格式与录像profile完整启动

boot 3162bfdb-7efc-4dea-a6bf-e6c186af6b73，initramfs
c737b8025adcef2056f99a23696c56f8000dbaa71654c7058633e5ed19fe4366，
provider d15efcb742debcf1bdcb016f6ff33dcf5975d581b2cdb7b789ff5df95cee6422。
当前Hybrid mapper8675c6b3…及allocator d928df42…沿用上一完整启动版本。
启动自动注册两camera，ro.media.xml_variant.profiles仍为_camera_dbf202n3，
实际选中的XML含两个ID的480p/720p/1080p H264+AAC profile，Java查询均返回对应尺寸。

v9在前后各公布320×240、640×480、1280×720、1920×1080及本sensor原生尺寸，
JPEG/BLOB33、PRIVATE34（实际RGBA）、YUV420_88835共用同一尺寸表和configure校验。
前摄2592×1944、后摄4224×3136的三种最大输出组合也均实机通过；
对应JPEG可解码且宽高正确，后摄最大JPEG约2.69MB。原生JPEG不是放大VGA图。
两路720p实际MediaRecorder录像已保存、Android读回尺寸/时长/音轨通过，1080p测试中。

保留initramfs.before-camera-formats-v9-20261115.cpio.gz=72d87b16…录像工作版本。
为ESP腾出空间，已移除过时BLOB-only备份before-camera-yuv-cold=0c9410e7…；
删除前与本地完整initramfs.camera-blob-cold.cpio.gz逐字SHA校验一致。
原图形7c27053b、拍照c9a81413、上版录像72d87b16和Mesa回退仍保留。
用户要求CCI偶发仅列问题，本轮不再追因，不以此阻塞格式接入。

## 2026-11-15：用户收束范围，推进照片/视频格式，偶发问题只列出

用户明确要求先推进视频和拍照格式及对接，CCI偶发问题列出不追究。
停止CCI定位，不再以该偶发问题阻塞当前接入交付。临时observe模块与kmsg读取均已停止，
第一次系统相机原触发序列再次前后录像/前拍照/切后摄通过，未做内核/供电修正。

v9在当前Hybrid上增加720p、1080p及原生照片尺寸，共享格式表约束HAL声明和configure。
完整RAW实测：前2592×1944（约34.9ms/帧），后4224×3136（约33.4ms/帧）。
ISP同一帧只解包/计算白平衡一次，同尺寸输出复用RGB；大图使用全像素双线性去马赛克。
JPEG容量按各sensor最大像素数计算；保持真实RGBA、YUV planes和BLOB footer路径。
720p和1080p preview+YUV+原生JPEG，前→后→前均通过，JPEG实测前约0.86MB、后约2.67MB。
前摄原生2592×1944三种输出也通过；后摄全尺寸三流及新的系统录像profile正在验证。
当前v9只临时更新HAL，未热换图形；新的480p/720p/1080p H264+AAC配置待完整启动。
软件ISP实际帧率随尺寸/组合下降，不将录像profile的标称30fps称为实测30fps。

## 2026-11-15：前后摄系统相机录像通过，切换偶发CCI超时仍在定位

当前boot 30736eb1-96c0-4391-98e4-853d5bdff3ae，完整启动包
72d87b16b2dcf013c652fdbea1a9d6e5601000e150656f032c5e26c2c289fc25已生效；
allocator d928df42…、mapper8675c6b3…、HALv8自动注册均核对，无图形热替换。
ESP保留initramfs.before-camera-encoder-20261115.cpio.gz=c9a81413…拍照工作包。

系统相机实际保存（DCIM/Camera）：
- 后摄20261115_221234583.mp4：640×480 H.264，41.303秒/748帧（约18.1fps），AAC音轨。
- 前摄20261115_221401016.mp4：640×480 H.264，9.713秒/310帧（约31.9fps），AAC音轨。
- 前摄JPEG20261115_221510340.jpg、后摄20261115_221541873.jpg再次保存。
视频包PTS/DTS均严格递增，全部帧解码通过；ffmpeg null默认编码时基曾报一次重复DTS，
保留源时基（-enc_time_base -1）后消失，源文件无重复时间戳。声音内容尚未主观听验。
证据stock-recording-verified.json、stock-encoder-front-stop.txt、encoder-photo-regression-log.txt。

前摄录完切拍照正常；随即切后摄时CCI master0 queue0 timeout，
OV13858首次software reset失败、HAL ERROR_DEVICE，app自动重开一次还frame timeout，
第三次恢复并保存后摄照片。故录像链路已通，不能称切换稳定性问题已解决。
停app后两sensor均suspended，未强卸载CCI/时钟/电源provider。
正常电源管理下后续18次RAW打开、9次Camera2三流短打开、各600帧前→后→前均通过。
临时只读kprobe观测超时前IRQ/队列，独立out内，不进启动包；未复现，第一次已卸载。
诊断app原切换延迟500ms，系统app约3ms；零延迟前→后→前各600帧及JPEG也通过。
两组共3600帧，均未再现超时，不能据此增加延时或常开规避。已结束对照、停止测试app、
卸载只读观测模块；两sensor和CCI均回到suspended。下一步需在原系统相机触发路径
取得超时前IRQ/队列证据；当前不改动未证实有错的供电/复位时序。
不据连续通过称根因修复，也未将CCI常开作为产品规避。

## 2026-11-15：系统相机双路预览/拍照通过，录像缓冲最小修正验证中

HAL stage v2完整启动boot 75677372-da16-470d-bcb0-7e9758a9c10f，
initramfs c9a814136176082059602b45d173d42f77fc51d26eb1049b91fa143807b1dfbc，
v8 provider 8d145ce4…自动启动、前后两ID正确枚举，原文件标签及两个640×480录像profile生效。
系统相机实际后摄预览→拍照→前摄预览→拍照→返回后摄通过；DCIM文件
20261115_215450215.jpg和20261115_215543102.jpg分别对应rear/front，日志URI36/37。
当前输出为软件ISP 640×480/320×240，画质/曝光与后摄对焦仍需改进，IR尚未出图。

短录像实际请求RGBA1+usage0x10033，旧wrapper白名单拒绝，已停止app避免错误刷屏。
原生QTI独立分配该usage成功；最初适配测试错误引用旧allocator头导致误判，已修正构建路径。
0006仅允许实际RGBA编码请求，原usage直接交给QTI；删除未必要的metadata v2/usage改写。
最终五种格式/usage分配、重复导入、metadata及全缓冲写读通过（hybrid-encoder-minimal-test.txt）。
mapper仍8675c6b3…不变，allocator d928df4220475573a2dabcc7cfd811676fd6441245eb3eda3dd799298ff36acd。
新完整启动候选72d87b16b2dcf013c652fdbea1a9d6e5601000e150656f032c5e26c2c289fc25正在部署；
不热换图形，保留c9a拍照工作包。录像尚未验收，不把原生缓冲通过等同录像完成。
历史CCI偶发resume超时未证明根因修复；另一次启动12次打开/三流及断电通过见下文。

## 2026-11-15：YUV三流与连续切换通过，HAL启动修正版已部署待重启

YUV boot e6407e1a…实测v6前→后→前preview+YUV+JPEG通过，mapper8675c6b3…、
allocator92c9cc9d…；未改GPU/图形服务。系统相机仍在打开前报invalid capabilities。
实际ro.media.xml_variant.profiles=_camera_dbf202n3，仅camera0的720p/1080p配置，
和当前HAL320×240/640×480不相交，camera1配置缺失。热改XML后native查询仍旧，
故改为Android init前安装匹配两ID的640×480/480p配置；录像本身尚未验证。

该boot第二轮v7测试切回前摄时CCI master1 queue0 timeout、OV5693软件复位-110，
runtime_status=error；停止HAL后只重绑OV5693能识别ID，但RAW仍超时，未强卸载provider。
v8把首次设备失败上报ERROR_DEVICE，停止后续请求反复启动坏的PM/采集状态；
这不等于修复CCI根因。720×480猜测已撤销；v8仍只320×240/640×480。
v8另明确readout timestamp unsupported，保留真实frame timestamp。

HAL首版完整启动包73ba5ef… boot a72888e4-0cb6-4e6f-a678-a02a542bb039：
camera-stage执行且v8 hash8d145ce4…正确，但init因可执行文件tmpfs标签拒绝启动；
手动restorecon按原file_contexts得到hal_camera_default_exec后双路枚举正常。
此外vendor.media.xml_variant.profiles=_spx，但ro属性已只读锁定为旧值；
首版variant函数覆盖无效，已从源码删除，不再追写只读属性。

当前stage修正版在post-fs恢复原HAL文件标签，修改已有相机variant XML里的
CamcorderProfiles段，保留原有Encoder/Decoder能力元素；离线真实XML变换检查通过。
启动修正版c9a814136176082059602b45d173d42f77fc51d26eb1049b91fa143807b1dfbc
已原子部署默认Hybrid并请求重启，当前等待上线。
pack-spx-camera-userspace.py仅5项变更，原身份/其他条目保持。
已验证YUV原包备份initramfs.before-camera-hal-20261115.cpio.gz=9d369e03…；
无新增失败73ba5ef副本占ESP（本地完整原件仍在out）。

在a72888e4启动中，restorecon后先完成一轮三流，再额外3轮前→后→前：
全部9次打开+JPEG通过，停流两sensor suspended，本boot无CCI timeout。
这段对照没有改变传感器/CCI驱动，不能据此将历史瞬时超时称为根因修复。
日志repeat-camera2-v8.txt、repeat-camera2-v8-power.txt。
新的build-spx-camera-graphics.py从实际source closure+原Hybrid链接命令复现补丁，
mapper二进制与实测8675c6b3…一致；allocator因assert源码路径不同hash不同，
未替换当前实测二进制。构建/镜像/照片/反汇编留ignored out。

## 2026-11-15：BLOB完整启动显示正常、Camera2复测通过；YUV候选重启待上线

BLOB候选boot 089c7f9c-da4b-49c6-9ebb-2c1cbae4a137已boot_completed=1，
用户确认显示正常；两处mapper 8278ecfc…、allocator 7802ea35…匹配。
只重新加载实验provider-v6（未热换图形）后，前→后→前各40帧预览/JPEG再次通过，
JPEG35518/39077/35578字节。此前闪烁/黑块只在热替换图形后观测到，
完整启动正常支持生命周期混用假设，但未证明所有显示场景长期稳定。

YUV35独立测试：原QTI metadata fd大小32768，seals=6，直接扩容EPERM。
最终0005补丁仅在flex YUV分配时导出独立memfd：原metadata页逐字复制，
追加请求格式描述并保留GROW/SHRINK seals；不改原QTI元数据字段或像素handle布局。
像素由QTI YV12分配，Stable-C分别报告请求35与实际YV12 fourcc/planes。
probe/camera/hybrid-buffer-test.cpp隔离测试RGBA、YV12、BLOB、YUV35的分配、
格式/plane元数据、重复导入和全缓冲读写均通过。未替换运行服务测试。
0005依赖0004，副本work/camera-android-20261115/hybrid-yuv，二进制同名out目录。

YUV完整启动包9d369e03c0e51dfedb609df9a04cef6b4a95937ac8e969b22e024073649811f5
已部署默认Hybrid并请求重启；本节写入时尚未上线。
备份initramfs.before-camera-yuv-cold-20261115.cpio.gz为已验证BLOB 0c9410e7…；
原版图形备份initramfs.before-camera-blob-cold-20261115.cpio.gz为7c27053b…。
两次只换initramfs四个图形条目，DT/菜单/Mesa不变，provider尚未持久化。

离线provider-v7增加真实软件缩放720×480，匹配系统现有480p录像配置；
明确readout timestamp unsupported（只有帧timestamp）。未部署验证。
Camera2 app加入可选width/height和实际EncoderProfiles日志。
先验证v6+YUV35三路流，再测v7/系统相机；不据能力声明声称录像已经通过。

## 2026-11-15：Hybrid Camera2 双路预览/JPEG通过；图形热替换出现显示回归，已撤回并请求重启

本次 boot e3c59494… 使用当前 Hybrid QTI gralloc1 + allocator5/mapper.quest2，
并非旧 Mesa/minigbm。新增 probe/camera 的动态 V4L2 后端、软件 RAW10 ISP、
AIDL Camera provider、Camera2 测试 app；前→后→前各40帧预览及JPEG保存通过。
工作图像为640×480，前后方向180度；后摄偏暗，颜色/清晰度尚未校准。
这只是测试 app 验收，不是系统相机/完整 Camera HAL CTS 通过；IR未接入HAL。

实验 provider-v6 临时 bind 到现有 usb/0 service，未写入启动包。
原Hybrid allocator不支持BLOB33和YUV35。隔离源码补丁
patches/camera/0004-hybrid-camera-blob.patch增加BLOB，独立分配/import/整块读写通过，
随后热替换mapper.quest2与allocator，使上述JPEG测试通过。
系统相机报 MainViewModel: Camera 0 does not have valid capabilities，尚未打开设备。
读取本地APK发现其 prepareCamera 要求照片及视频用例组合均非空；未据此冒充视频支持。
YUV35仍未实现；不可绕过AIDL约束强制覆盖显式YUV格式为YV12。
spx-buffer.h按Stable-C metadata导入真实stride，并通过sphal共享同一mapper实例。

用户随后报告桌面闪烁/黑块。已停止两相机app和实验provider，撤回两图形bind，
校验原allocator f6fab31f35a27d7843b8fcc4e45de0b3fc0f0ce26bf9453d4c7d7140cb9172bf、
原mapper 8af79a070311b38bb0890a4ef0f6e96a186f923a83e7ed075dd199e5d5edbc0f；
撤回provider bind并启动原服务。普通umount返回EBUSY，停止图形服务后使用umount -l；
服务重新running但用户报告黑屏，随后通过sys.powerctl请求完整重启。
当前等待新boot/显示恢复。不得将此候选称为可部署图形修复。
热替换导致进程库/缓冲混用是待验证假设，不是已证明根因。
后续图形候选需完整启动验证，不再热换活跃mapper/allocator链路。

默认持久DT 0dfb6dc7…、initramfs 7c27053b…及Mesa回退未改；
仅此前内核摄像头模块自动加载已持久化。图片、APK反汇编和日志均留ignored out。
构建/测试/回退原始证据位于out/camera-android-20261115。

## 2026-11-15：前后摄 RAW 实景出帧，重启自动加载与双路取帧通过

SQ2 boot f7073371… 已实测前摄 OV5693 接 PHY0、后摄 OV13858 接 PHY2；
前摄1296×972三帧，间隔约17.4ms；后摄2112×1568三帧，间隔约33.4ms。
均经真实传感器→CSIPHY→CSID0→VFE0 RDI0→DMA，非内部TPG。
已解包检查两张RAW，能看到实景；后摄曝光较暗，色彩/方向未校准。
停流后两sensor runtime suspended、未见SMMU fault。
Android现有usb/0 provider仍枚举0个摄像头，尚未完成Android接入；IR暂后置。

修正内容：CAMSS接收端data lane从0编号、clock lane7；补PHY公共
camnoc_axi/slow_ahb_src/cpas_ahb时钟；按Windows原生表将SC8180X数据lane
CTRL0由0x90改0x91。GCC_CAMERA_AHB由GCC强制开启，不应请求未导出的clock242。
物理PHY连线来自PCFG解码和实际出帧：后摄slot0的bits20..23为2；
Windows前摄驱动读取PCFG slot1，其字段为0。模组cameraId不能当PHY编号。
此前借用cameraId的前PHY2/后PHY0及frontPHY3候选均不能出帧。

内部TPG640×480先连续出3帧，已证明后端链路；随后TPG关闭才做实景验证。
Windows原始反汇编、RAM overlay、抓帧工具/图片均在ignored
out/camera-v4l2-20261115，不进入公共源码或启动包。
工作CAMSS模块为modules-camss-phy91；sensor依赖为modules-final。
生成器prepare-spx-camss-dtb.py已改为前0/后2，原属性/保留内存断言通过。
默认持久DT已更新0dfb6dc77477942bbe71ce239de6d475ba50e1421db6bd382efc54841f12deb4，
23e1ba83旧DT备份为android-el2.before-camera-frames-20261115.dtb。
随后boot e3c59494-16cf-47da-aa15-023ad81a8b08已由离线DT启动，未加载RAM overlay。

新增spx-camera-boot与pack-spx-camera.py，注册驱动而不自动采集。
候选initramfs7c27053bf5fbb5ce814487586bd218471d1460aae0b2da2e1c518f9cdb1679fc，
基线3d7ecee4…；仅14项变化，所有其他条目与本机凭据逐字保持。
只打包10个运行模块，不含ACPI/ID/MMIO探针或i2c-dev；sq2.camera=0跳过。
启动包已部署并重启验收：sys.boot_completed=1，Wi-Fi/SSH恢复；
10个模块自动加载、/logs/camera-boot.txt无错误。前后摄各重复三帧成功，
时间间隔与热加载一致，停流2秒后两颗sensor suspended，未见SMMU故障。
备份initramfs.before-camera-autoload-20261115.cpio.gz为原3d7ecee4…。
当前媒体video0是VFE0 RDI0、video3是Venus；CCI bus也由3/4变为2/3。
这些是本次枚举结果，后续HAL必须按实体/设备名发现，不能硬编码节点编号。
原Mesa入口与菜单保持。4项DT测试、真实ARM64模块编译及组包断言通过。

## 2026-11-15：CAMSS完整DT已部署重启，媒体图待验收

RAM overlay已使CAMSS加入IOMMU group16（DMA-FQ），但probe失败Cannot get remote parent：
live overlay未给既有sensor endpoint加入phandle。没有启流/DMA，不当作媒体图成功。
离线完整DT通过原属性/保留内存保持及双向endpoint phandle断言，
SHA23e1ba836383500c573614ca9a4b8b7021fbf02ad8c21b7bec4265bbabee653d。
重新识别本次ESP/物理NVMe/根loop源后，备份a18d99b1到
EFI/SQ2Hyb/android-el2.before-camss-20261115.dtb，原子替换默认DT并请求重启。
initramfs SHA3d7ecee4…、菜单及Mesa回退保持。当前等待新boot联网，尚无本DT启动结果。
接着加载modules-final中的电源/CCI/V4L2/两sensor，以及CAMSS+videobuf2-dma-sg，
确认媒体图与IOMMU后再测RAW。备用地址10.126.126.4，首选192.168.8.125。
新生成器scripts/prepare-spx-camss-dtb.py；部署/日志在out/camera-v4l2-20261115。
上节“尚未加载”已被本节及上述RAM绑定结果覆盖。

## 2026-11-15：前后摄V4L2传感器绑定/电源循环通过，CAMSS继续适配

当前boot ba6eaa68…，Hybrid持久DT a18d99b1…，备份
android-el2.before-v4l2-sensors-20261115.dtb 为此前SPMI ID版0745efda…。
新增前后摄I2C节点替换三路ID消费者；GPIO/CCI/供电沿用已验证路径。
初次加载缺v4l2-async/fwnode，补现有匹配模块后后摄绑定成功。
前摄原上游顺序两次在master1 queue0超时；改为已成功的原生顺序：
电源→1ms→MCLK→1ms→释放reset→10ms，绑定成功。补丁仅用于实验模块副本。
前后驱动均完成一次runtime active→suspended，前摄resume的模式初始化无新I2C错误；
空闲后A-LDO1/14/17 disabled且引用0。这仍不是传感器出图或Android相机可用。

新增0001后摄OF电源、0002前摄上电顺序、传感器DT生成器；camera-probe builder
补齐v4l2-async/fwnode依赖，共12模块，匹配ARM64编译/MODPOST及ACPI ASan/UBSan通过。
产物out/camera-v4l2-20261115/modules-final；运行前摄为同源sequence候选模块。
运行传感器模块未持久化到initramfs，重启后需手动加载；不要误以为自动启动已接通。

本机IORT只读导出成功（6467字节，校验和有效），摄像头映射位于GPU0.AVS0命名节点，
不是VFE0；包含16个a00..ae0/e00..ee0精确SID，不能复制SM8150的SID mask。
独立临时consumer上电实读VFE0=10020001、CSID0=10000000、CSIPHY0=10000000。
PHY读版本仅临时写SHOW_REV_ID并还原；未reset/启动DMA，节点/overlay已移除。
诊断脚本、原始表和日志均在ignored out/camera-v4l2-20261115。

0003 CAMSS初版仅暴露4 PHY + 1 CSID + IFE0的RAW路径，按SC8180X实际频率表；
参考Gaurav Suman/Andrew Gigena的SM8150 Gen2实现，使用已有v1.1 lane表仍待真机取帧确认。
CSI前PHY2/后PHY0只是当前待验证候选，不把cameraId当作已经证实的物理连线。
build-spx-camss.sh构建qcom-camss和所缺videobuf2-dma-sg；
真实编译/MODPOST通过。媒体图、接收中断、DMA/帧、Android HAL仍待验收。
CAMSS RAM overlay准备完毕，首次传输前SSH超时，本节尚无CAMSS加载结果。
持久ESP/initramfs/菜单/Mesa回退未再改。当前主目标前后摄可用仍未完成。

# Surface Pro X 前后摄像头与 Windows Hello 红外实验

## 2026-11-15：SPMI供电与前后/红外三路芯片ID真机通过

用户要求参考Windows驱动继续电源排查与三路真机测试。独立SPMI方案已在
SQ2 boot c1c7cdea…启动并实测，Wi-Fi始终可连接；原RPMh A provider保持绑定。
当前Hybrid DT为0745efda…，原81b43321…在before-spmi-camera-20261115.dtb。
只热加载实验模块，未改initramfs/启动模块列表/菜单/Mesa回退或Android provider。

| 路径 | 实际芯片ID | 实测CCI路径 | 7-bit地址 |
| --- | --- | --- | --- |
| 前置OV5693 | 0x5690 | CCI0 master1（GPIO19/20） | 0x36 |
| 后置OV13858 | 0x00d855 | CCI0 master0（GPIO17/18） | 0x10 |
| 前置红外OV7251 | 0x7750 | CCI0 master0（GPIO17/18） | 0x60 |

CCI0/1均读得0x10070000、各自复位中断通过。每路仅在ACPI声明地址读0x300a，
按ACPI顺序复位/供电/MCLK后取ID并断电；其它master上读回-ENXIO，未扫未知地址。
模组二进制cameraId/PCFG索引不能替代上述实际接线结果。
SPMI provider使用上游qcom_spmi-regulator的真实subtype自动识别，新增的实验
表仅含A-LDO1/14/17，分别1200/1800/2856mV，不再给RPMh provider加这三路。
已卸载ID与只读探针。读回4046/4d46/5046全部00，三路电源关闭；
两个CCI runtime=suspended，原Wi-Fi及RPMh A provider保持正常。

这是供电、复位、时钟和I2C通信通过；未配置模式表/CSI/VFE/ISP，仍无原始帧，
Android枚举0。红外补光、Windows Hello/Android人脸认证未启用。
下一阶段先补SC8180X CAMSS/CSI/VFE资源和sensor媒体端点，前/后/IR分别取帧；
不能把已有sensor模块或USB Camera HAL当作内置摄像头出图完成。

证据out/camera-power-20261115：boot.txt、regulator-load.txt、cci-load.txt、
sensor-id.txt、after-test.txt；Windows反汇编和原始寄存器记录保持ignored。
构建入口增加SPMI参考driver和只读电源探针；板级DT断言测试与ARM64编译通过。

## 2026-11-15：摄像头电源改走独立SPMI，已读出真实LDO类型

Windows参考qcpep8180含PmicVregVote、PppSetSpmiConnectionId、
DispatchPmicRequest与ConvertLdoToRpmhResource路径；本机ACPI的三路供电/时序继续作为
板级依据。反汇编仅建立控制路径线索，不声称已完整还原Windows PMIC服务实现。

在既有Linux SPMI controller下读取pmic@1 regmap成功，无寄存器写入：
LDO1A base4000 type04/subtype6b，VSET=04b0(1200mV)、mode07、enable00；
LDO14A base4d00 type04/subtype72，VSET=0708(1800mV)、mode07、enable00；
LDO17A base5000 type04/subtype7d，VSET=0b28(2856mV)、mode07、enable00。
分别匹配上游qcom_spmi-regulator的N600_510、LV_P300_510、MV_P600_510。
三个cmd-db地址实际携带SID1+PMIC peripheral地址；不能按VRM vote偏移访问。
只读模块已卸载。证据out/camera-power-20261115/read-result.txt。

builder复制上游SPMI regulator源码，仅新增camera-only资源表与实验compatible，
保留实际类型检测和SPMI arbiter权限检查。DT改为pmic@1/camera-regulators三路独立节点，
现有RPMh A/E/C所有属性不变。新DT 0745efda…已原子部署并请求重启，
原默认81b43321…备份before-spmi-camera-20261115.dtb；电源/ID加载结果待补。
新的SPMI候选取代已失败RPMh生成实现；旧脚本快照保存在ignored out，未改主线启动默认。
真实ARM64九模块编译/MODPOST、ACPI ASan/UBSan、板级DT保存与供电引用断言通过。

## 2026-11-15：回退重启验收通过，Wi-Fi恢复

用户完成手动重启，新boot6b539bab…，Android boot_completed=1。
通过Wi-Fi地址192.168.8.125重新SSH连接，wlan0 UP/LOWER_UP且取得该地址；
A-PMIC provider恢复绑定，USB控制器驱动恢复绑定。采集中一次SSH连接超时，
随后同boot成功复连；本次仅确认联网恢复，不宣称长期网络/休眠稳定性。
默认Hybrid DT为81b43321…，运行CAMCC phandle=0xbc，三路临时camera-id节点不存在。
USB仅检查驱动绑定，不宣称外设传输测试通过。摄像头仍未出图，三路ID均未读回。
保留CAMCC/CCI0控制器已通过的结论；失败的A组电源VRM方案不纳入默认构建。
需先确定其实际供电控制接口再做传感器测试。
证据：out/camera-deploy-20261115/rollback-verified.txt。

## 2026-11-15：EasyTier/LTE恢复访问，已回退故障摄像头DT

用户通过EasyTier+LTE提供10.126.126.4；严格原SSH主机身份校验通过。
仍在第二轮Hybrid实验boot1735694a…，未切Mesa；默认DT为d8886c1c…。
现场证据：A-PMIC regulator provider未绑定，Wi-Fi等待A-LDO9，
USB PHY等待A-LDO12，LTE/E组/C组仍工作。不是Wi-Fi凭据丢失。
cmd-db包含ldoa1=0x14000、ldoa14=0x14d00、ldoa17=0x15000（aux=0），
不同于既有正常VRM资源0x4xxxx。一次针对未绑定provider的重试捕获：
LDO17A约束选到2.856V后，任务阻塞在rpmh_regulator_vrm_set_mode
→rpmh_write→rpmh_rsc_send_data；未完成整组注册。资源存在不等于支持VRM寄存器协议，
不能继续将这三路直接接入现有RPMh regulator，也不能改用E组同号电源。
未加载sensor-ID模块，未读取三路ID、未启流/补光。

已核对当次磁盘/ESP/系统根源，原子恢复Hybrid的android-el2.dtb为
第一轮已正常启动的81b43321…（仅CAMCC phandle），并请求重启。
initramfs、菜单、Mesa回退、用户数据保持。重启后的Wi-Fi验收待补。
原始证据：out/camera-deploy-20261115/{lte-reconnect,failed-dt-resources,
pmic-bind-filtered,pmic-bind-stack,restore-phandle-dtb}.txt。
三路资源候选d8886c1c…是失败实验，不纳入默认构建；下一步须先确定
A组摄像头电源的实际控制接口，再恢复传感器ID工作。


## 2026-11-15：用户授权部署，CCI0 真机控制器测试通过

用户最新明确“部署并测试，允许后续的真机操作”，覆盖此前仅保留候选的暂停边界。
最小CAMCC phandle DT已备份/原子部署并重启：boot5041c1dc…，
Android boot_completed=1，运行phandle=0xbc。原Mesa回退、菜单和initramfs未改。
匹配CAMCC/CCI模块加载后，CCI0(0xac4a000)读得HW_VERSION=0x10070000，
GIC GSI492复位中断1次，两条Qualcomm-CCI适配器注册并runtime-suspend。
动态overlay通知因CCI抢先绑定，将i2c-bus节点误当client，返回-EINVAL但节点仍存在；
因此改为离线板级DT后重启，不将该错误当作干净加载成功。

供电核对纠正：旧DT的LDO14为PMIC E，不是摄像头所需PMIC A。
新prepare-spx-camera-test-dtb.py只增加A组LDO1/14/17、CCI0/1、ACPI针脚及临时
ID消费者节点，保留原属性/保留内存；前后/红外尚未读回ID。
A-LDO17按RPMh 8mV步进将2.85V请求设为2.850–2.856V；不改E组电源。
spx-camera-id按ACPI顺序逐路上电，在两个CCI的四个master只读已知地址的ID，
完成后拉低reset、关闭MCLK并撤销电源引用；不发送模式表、不启流/补光。
已编译/MODPOST；板级DT资源/原属性/保留内存测试通过。
新DT d8886c1c…部署后的第二次重启已进入Android桌面，但用户确认Wi-Fi无法连接，
SSH断开、无USB调试通道。尚未加载ID探针，三路ID均未测；不能归因于传感器上电。
正在通过未修改的Mesa Fallback恢复网络，再还原默认DT并取证；不推广本板级候选。
原件为ESP中android-el2.before-camera-20261115.dtb(91a2c5eb…)；
中间phandle候选备份before-sensor-id-20261115.dtb(81b43321…)。
本轮证据在ignored out/camera-deploy-20261115。

## 2026-11-15 扩展为三摄像头实验

用户要求将后摄像头和Windows Hello一起纳入本轮。当前范围包括前置彩色、
后置彩色、Windows Hello使用的前置红外摄像头及其补光控制识别。
用户后续已授权部署及后续真机操作；最新结果见下节及HANDOFF，历史暂停记录不再限制本轮。

| 路径 | 参考模组 / ACPI身份 | 候选7-bit地址 / ID读法 | 本机D0电源与复位 |
| --- | --- | --- | --- |
| 前置彩色 | OV5693 / CAMF / OVTI5693 / MSHW0180 | 0x36；0x300a起两字节，期望0x5690 | GPIO12；LDO14A 1.8V；MCLK2 19.2MHz |
| 后置彩色 | OV13858 / CAMS / OVTID858 / MSHW0181 | 0x10；ACPI从0x300b期望0xd855；上游驱动从0x300a读三字节0x00d855 | GPIO22；LDO14A 1.8V、LDO17A 2.85V、LDO1A 1.2V；MCLK0 19.2MHz |
| 前置红外 / Hello硬件 | OV7251 / CAMI / OVTI7251 / MSHW0182 | 0x60；0x300a/0x300b期望0x77/0x50 | GPIO23；LDO14A 1.8V；MCLK3 19.2MHz |

身份来自SQ2本机DSDT、SQ1参考INF与模组名称交叉核对。
OV7251的原厂INF明确命名Surface IR Camera Front。上述地址、期望ID是待读回的
参考值，不能作为已识别硬件结果；也不能将芯片名称中的7251误写为期望ID。
三路共享电源/控制资源，切换时需要由regulator/clock框架保持其他使用者引用。
后置LDO17A/LDO1A必须按本机定义接线，不能套用前置的单电源序列。

Windows Hello实验分开记录三个结果：

1. OV7251芯片ID、连续原始帧与曝光控制。
2. 红外补光的实际控制资源、原厂时序及与取帧的关系。
   当前仅发现CAMP.PRLD隐私指示灯配置线索，尚未证明红外发射器接线；
   不将隐私灯等同补光灯，不尝试猜测GPIO或常亮补光。
3. 人脸认证接入可行性和平台选择。红外出图不等于Windows Hello认证可用；
   当前Android上的人脸解锁需要单独的生物识别软件/HAL集成与验收。
   本轮未接入识别、未注册人脸、未改锁屏或认证策略。

统一推进顺序：核实CCI主通道/针脚 → 三路分别读ID、单路启停 → 三路分别
V4L2连续取帧 → 前后彩色图像处理/Android Camera HAL与前后切换 →
红外曝光/补光验证 → 单独评估认证集成。三路并发不是首次出图的前置条件。
每路分别记录成功、失败、未测，某一路出图不代表其它两路通过。

### 三路离线驱动候选

build-spx-camera-probe.sh现同时编译ov5693、ov13858、ov7251、v4l2-cci、
camcc-sc8180x、i2c-qcom-cci及只读ACPI导出模块。真实ARM64编译和完整该模块组
MODPOST通过，ACPI主机ASan/UBSan断言通过，输出：
out/camera-three-sensors-20261115/modules；日志同级build.log。
这是上游传感器参考模块构建，不是已适配Surface的可部署摄像头栈。
其中ov13858上游probe依赖ACPI先上电；当前acpi=off，需要补DT及显式供电/
复位生命周期后才能实测，不能仅增加I2C节点就声称已接通。
OV7251/OV5693也需实际供电、端点、lane与频率配置，后端CAMSS缺口保持。

OV5693依赖V4L2寄存器访问帮助库，builder只为ov5693/v4l2-cci两个对象开启
CONFIG_V4L2_CCI_I2C编译分支，提供该库的I2C函数；不修改匹配构建的.config。
这是V4L2 CCI帮助库，与高通i2c-qcom-cci总线控制器是不同模块。
本轮未把这些新模块复制到设备、未增加传感器DT节点，原phandle候选保持原样。

## 首轮前置调查记录（2026-11-15）

用户提供private/sq1-camera-reference-20261113-ece8b7fa-payload.zip，要求尝试前置摄像头。
已连接用户指定SQ2（当次地址192.168.8.125），运行Linux6.18.60-sq2-android1、SQ2Hyb。
前置摄像头尚不可用：Android枚举0；唯一video0为qcom-venus-decoder，不是摄像头。

参考包151项manifest全部匹配，原二进制和设备表只保留private/out。
当前SQ2经只读RAM模块取得DSDT，402398字节，校验和正确：
53a0daad0250847edc0f2329ec840d04065160025af542f9ab60e2a93e60046e。
参考SQ1 DSDT为402442字节、454e7cf5…；全表并不相同，摄像头区段一致，
差异在平台身份/其他设备。不能把整机或所有硬件修订等同。

已核实的前置资源：

- CAMF：OVTI5693 / MSHW0180；模组ffc_ov5693。
- SCFG：0x0015006c、0x5690300a，对应候选7-bit地址0x36、ID寄存器0x300a、期望0x5690。
  地址/ID来自原厂定义，尚未由I2C读回。
- D0顺序包含MMCX、CAMCC/TITAN、GPIO12先低、LDO14A 1800000µV、
  延时、MCLK2 19200000Hz、GPIO12拉高。D3反向撤销。
- CAMP资源有CCI窗口ac4a000/ac4b000/ac4c000/ac4d000；
  MPCS有ac65000至ac68000四个CSI PHY窗口。
- 参考模组二进制结构中cameraId=2；这不直接证明CCI控制器编号。
  SCFG高位0x15三个传感器相同，不能未经验证解释成前置专属CCI通道。
- CCI通道、引脚选择、CSI连接仍需进一步解析/实测；未扫描总线或尝试未知引脚。

## 已完成的实机步骤

运行配置与保留6.18.60 native-kernel-v3构建配置仅LOCALVERSION不同。
新独立work/camera-front-20261115/build使用实机完整配置，olddefconfig后逐字一致，
prepare及外部模块编译/MODPOST通过；原源码/旧构建不改。
复用该6.18.60构建的Module.symvers；运行MODVERSIONS关闭。
CAMCC的17个、CCI的35个导入符号均在实机导出表存在。
这不是完整运行Image可复现性证明；旧6.3.8模块不能用于当前设备。

在boot5ba2cab2…仅加载上游camcc-sc8180x.ko，成功绑定ad00000.clock-controller。
未加载CCI、未上电/读写传感器、未启流。绑定通过不等于图像链路通过。
只读spx-camera-acpi模块通过EFI ACPI RAM描述符验证RSDP/XSDT/FADT/DSDT，
导出后已卸载，没有调用固件运行服务或写硬件寄存器。
随后连接失联，用户明确确认手动重启；新boot36491175…，临时CAMCC已不在。
不能把这次手动重启写成驱动自动崩溃。

## 保留的候选及暂停边界

现有CAMCC节点没有phandle；动态CCI节点无法引用这个已注册provider。
prepare-spx-camera-dtb.py只在离线DTB为唯一SC8180X CAMCC分配未占用phandle，
不增加CCI/传感器、不修改原有phandle，不改保留内存和其它属性。
本次候选：
out/camera-front-20261115/boot.camera.dtb
SHA256 81b4332101041e8455b935a12565f8e4e70ea85dd977f61d778690b45bb912cb。
原启动DTB：
out/camera-front-20261115/boot.before.dtb
SHA256 91a2c5eb4d5cbec020469f51158852f6e1c789ba47da4832e4fa6dc14d8933fc。

用户明确选择“先保留候选，暂不重启”。没有写ESP、改菜单或initramfs，未部署候选。
后续用户允许实验启动后，再核对当次目标ESP和原DT，备份/原子替换并保持Mesa回退，
启动后按实际DT资源构建CCI测试。不要把这份实机派生DTB分发为通用镜像。
先确认控制器/主通道及针脚，再只读预期0x36的芯片ID；失败保留真实错误。
之后仍需SC8180X CAMSS/CSI/VFE、OV5693和Camera HAL链路。
当前6.18源码CAMSS没有SC8180X资源表；仅启用OV5693或USB provider不会让内置前置出图。

## 构建与检查

    scripts/build-spx-camera-probe.sh KERNEL_SOURCE MATCHING_BUILD NEW_OUTPUT
    python3 scripts/prepare-spx-camera-dtb.py ORIGINAL_BOOT.dtb NEW_CAMERA_REFERENCE.dtb
    python3 -m unittest discover -s tests -p test_spx_camera_dtb.py -v

builder现构建上游CAMCC、CCI、三路传感器及V4L2 CCI帮助库和只读ACPI模块，不安装、不加载。
MATCHING_BUILD必须与目标内核配套，包含.config、准备好的头文件和完整Module.symvers。
本轮实际命令使用：
work/qualcomm-618/out/kgsl-sde-port-20261110/native-kernel-v3/source，
work/camera-front-20261115/build，
out/camera-front-20261115/repro-modules。
ACPI解析器主机ASan/UBSan断言、DT三项测试、真实ARM64编译/MODPOST均通过。
DT测试包含已有引用幂等、动态分配、错误平台/歧义拒绝与保留内存不变。

原始证据在ignored out/camera-front-20261115：
runtime-baseline.txt、runtime.config、camcc-load.txt、acpi-load.txt、
sq2-dsdt.dat/.dsl、repro-build.log及两份experiment-*.txt。
所有硬件原始表、驱动二进制、构建产物保持不进Git。
