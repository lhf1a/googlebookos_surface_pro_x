## 2026-11-04：接回81c5c67的本机LTE自动采集构建链

用户明确要求按81c5c673bb695a71a9017c2cf1bf6bd6ddf457aa修复标准通用构建。
此项覆盖下方“保留旧LTE兼容/FSG打包路径”的合并决策：默认EFI router在EL2前采集
本机FSG并使用本机UUID缓存，RAM seed核验来源/CRC后提供RMTFS，Radio为唯一bearer owner。
构建不读取或嵌入modem_fsg.bin、unit.json、单机APN/serial；使用源码AOSP APN表。
恢复上游退休的旧Java NetworkAgent删除与其旧基线测试删除。正常unrestricted和
screen_only=0、nohibernate、共享固件、双机档位、Mesa回退保持，不恢复must-Y门禁。
UUID只是动态缓存归属，不是样机白名单；实际缓存含私有校准，不能分发。
本次只构建、不部署/重启；LTE构建通用化不证明双机新整镜像/长睡稳定性。
标准组包仍使用指定模板和runtime中的原有SSH身份；本地产物不可当匿名公共镜像发布。
U4阶段候选仍是独立打包入口，不宣称标准镜像已自动包含全部U4诊断覆盖层。

> 2026-11-04更新：下文lte-fs12-seed、lte-fsg-seed及el2-preboot-init相关旧诊断工具已退役；引用仅为历史记录，当前实现见docs/HANDOFF.md顶部。

## 2026-11-04：当前U4开发版本合入主线（未部署）

按用户“冲突保持现在能力和启动流程”：保留unrestricted策略、Venus system PM、QCA最终
失败清理、GENI UART补丁、SPI/FUSE/音视频既有链路。7处文本/删除冲突选当前版本；另
解决LTE语义冲突：不以主线新EFI FSG/router替换当前启动路径，保留当前LTE兼容工具及
打包依赖。上游新增LTE实现保留为尚未接入默认的源码；旧主线“已默认接入”描述在本合并
中不代表当前入口。主线触控v7及对应改动保留，未作本机重新部署或声学/触控验收。

U4固定默认仅指上轮私有离线ZIP；包、身份、日志均不入Git，设备仍原SQ2Free。
pm_test=devices诊断包不改为真实连续睡眠；未冷启动WSA helper源码仍明确标候选。
十分钟静置无响应仍未解决，不以合并代码替代实机验收。不写ESP、不重启、不推送远端。


Current source commit: 299f689350d89983fbfa918b7ff5118f2b79ea07
Incoming main: 61eabf1eaa695f00acbd010b50a876bbcd35512e

Resolved conflicts:
- `docs/BUILD.md`
- `docs/HANDOFF.md`
- `probe/test2/lte-fs12-seed`
- `probe/test2/lte-fsg-seed`
- `tests/test_sq2_lte_fs12_seed.py`
- `tests/test_sq2_lte_fsg_seed.py`
- `tests/test_surface_lte_baseline.py`

Preserved current compatibility paths:
- `probe/lte/Sq2Lte.java`
- `probe/lte/lte-framework-boot`
- `probe/lte/lte-unit-seed`
- `probe/lte/radio-stage`
- `probe/lte/sq2-qmi-data.c`
- `probe/lte/sq2-qmi-mode.c`
- `probe/lte/sq2-radio.cpp`
- `probe/lte/sq2-rmnet-create.c`
- `probe/lte/stubs/android/net/NetworkAgent.java`
- `probe/lte/stubs/android/net/NetworkAgentConfig.java`
- `probe/lte/stubs/android/net/NetworkProvider.java`
- `probe/test2/sq2-ipa-acpi.h`
- `probe/test2/sq2-ipa-gsi-read.h`
- `probe/test2/sq2-ipa-gsi-state-read.c`
- `probe/test2/sq2-ipa-iort-read.c`
- `probe/test2/sq2-ipa-smmu-snapshot.c`
- `probe/test2/sq2-ipa-version-read.c`
- `probe/test2/sq2-ipa-version.h`
- `probe/test2/sq2-lte-online-test.c`
- `probe/test2/sq2-mfs-readonly-lab.c`
- `probe/test2/sq2-pdc-own-profile-lab.c`
- `probe/test2/sq2-pdc-readonly.c`
- `probe/test2/test-lte-readonly`
- `probe/virtualization/el2-rmtfs-start`
- `probe/virtualization/spx-model-select.c`
- `probe/virtualization/sq2-lte-efi-snapshot-v2.c`
- `probe/virtualization/sq2-lte-efi-snapshot.c`
- `probe/virtualization/sq2-lte-gpt.h`
- `scripts/build-lte-source.py`
- `scripts/build-sq2-ipa-gsi-state-read.sh`
- `scripts/build-sq2-ipa-iort-read.sh`
- `scripts/build-sq2-ipa-version-read.sh`
- `scripts/build-sq2-lte-data.sh`
- `scripts/build-sq2-lte-efi-v2.sh`
- `scripts/build-sq2-lte-efi.sh`
- `scripts/build-sq2-lte-tools.sh`
- `scripts/build-sq2-mfs-readonly-lab.sh`
- `scripts/build-sq2-pdc-own-profile-lab.sh`
- `scripts/build.py`
- `scripts/inspect-sq2-ipa-iort.py`
- `scripts/pack-sq2-lte-data.py`
- `scripts/pack-sq2-lte-fs12.py`
- `scripts/pack-sq2-lte-fsg.py`
- `scripts/prepare-sq2-lte-once.py`
- `scripts/surface-lte-payload.py`
- `tests/sq2_ipa_acpi_test.c`
- `tests/sq2_ipa_gsi_read_test.c`
- `tests/sq2_ipa_version_test.c`
- `tests/sq2_lte_gpt_test.c`
- `tests/sq2_pdc_session_test.c`
- `probe/lte/sq2-radio-query.cpp`
- `tests/test_sq2_ipa_iort_read.py`
- `tests/test_sq2_ipa_smmu_snapshot.py`
- `tests/test_sq2_ipa_version_read.py`
- `tests/test_sq2_lte_gpt.py`
- `tests/test_sq2_lte_once.py`
- `tests/test_sq2_lte_online.py`
- `tests/test_sq2_mfs_readonly_lab.py`
- `tests/test_sq2_pdc_own_profile_lab.py`
- `tests/test_sq2_pdc_readonly.py`
- `tests/test_sq2_radio.py`

Validation limits: the LTE baseline test fails identically before and after merge
(two failures/two errors) because private libbinder_ndk.so and cached DT fixture
are absent. No production guards were weakened to hide this. Focused power,
legacy LTE seed, touch and startup checks are recorded separately; no hardware
acceptance is repeated and no claim of a full firmware build is made.
