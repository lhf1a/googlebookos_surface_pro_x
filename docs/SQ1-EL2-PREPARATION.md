# SQ1 EL2 接管：离线准备归档（2026-10-31）

## 状态与许可

已完成签名 TCB 回调路径定位和本地主机构建；**尚未运行 EFI、验证固件接受载荷或启动 EL2 Linux**。用户允许准备独立测试介质，测试前由用户确认现场就绪。不修改默认启动、不自动关闭 Secure Boot、不重启、不写未识别的磁盘。本文不是实机成功报告，也不将阶段 1/2 全部标为完成。

Windows 参考设备为 Surface Pro X / Microsoft SQ1，Windows 22631、UEFI 4.234.140。只读采集显示 HypervisorPresent=true、VBS 状态 2、Secure Boot=true。这些不能证明当前 Linux 引导路径取得 EL2；CPU WMI 的虚拟化标志不能单独否定硬件能力。连接资料和凭据保留在 private/，不提交。

## 离线结果

- 参考 tcblaunch.exe 版本 10.0.22621.6199；完整样本 SHA256 见准备清单。
- 微软公共符号：tcblaunch.pdb GUID `72D3686FBF8403002EE3B786C2B314E6`，RSDS age=2、DBI age=2、info age=3。最初要求 info age=2 的检查失败；修正为记录两者、校验 GUID 和 DBI age，并对照实际指令。
- image base=0x400000：BlSlEntryPoint 0x427488 → SlpQcEntry 0x427150 → SlpEntryPoint 0x427028。
- Qualcomm 分支保存 tz_data 到 context+96，设置 kind=1。初始化/启动参数失败路径调用 SlpEnterEL1 后，0x427100 取 tz_data、0x427104 计算参数 tz_data+0x68、0x427108 取回调 tz_data+0x48、0x42710c 调用回调。
- 因此该签名版本保留 slbounce 需要的回调机制。更早的认证、映射和 MDL 检查仍可能失败，不能据静态代码保证固件接受。
- winload 对照：0x1800c0d58 经 0x18003fcc0 执行 Secure Launch 命令 1→2→4；SMC ID=0xc3000001。0x1800c1178 设置 tz+0x48 回调、tz+0x68 起的保存上下文，与上述布局一致。
- REA 只用于本地分析。目标 0x427028 的反编译返回 No matching function；本结论来自公共符号和实际 GNU 指令反汇编，不冒称 REA 已成功反编译该函数。

候选链：UEFI EL1 → slbounce ExitBootServices hook → 签名 TCB Secure Launch → Qualcomm 失败回调 → tb_entry/HVC1 → EL2 → Linux。固件接受、全 CPU EL2、KVM 和外设均待现场验证。

SC8180X 源码检查：PSCI 使用 SMC；apps/GPU SMMUv2 分别在 0x15000000/0x2ca0000，不能套用 SC8280XP SMMUv3 overlay。qcom-smmu 对 BYPASS 写回进行运行时 quirk 检查；SC8180X 使用 arm_mmu500_reset。remoteproc 接管仍需单独解决；所查 qebspil 没有 SC8180X 支持。未取得固件样本，不声称完成固件审计。不要把 SQ2 高频 DTB 用作 SQ1 基线。

## 提交的必要材料与复现

- `configs/sq1-el2-preparation.json`：源码 pin、输入和本次构建产物哈希；不是不同工具链可复现性保证。
- `scripts/verify-sq1-el2-preparation.py`：只读检查本地样本和 EFI，不执行目标、不连接设备、不写盘。
- 原始日志、PDB 及构建 EFI 保留在忽略目录 `out/virtualization-feasibility-20261031-195101/stages12/`，不进入 Git。用户随后于 2026-10-31 明确授权将四份已提取的 Windows 二进制加入提交并推送，精确副本及哈希归档在 `reference/sq1-el2/windows-22621.6199/`。不附带凭据，不下载来源不明的旧 TCB；微软文件不适用本仓库源码许可。

WSL 中安装好本机原有交叉编译工具后，在独立忽略目录检出 slbounce 清单指定提交，执行 `git submodule update --init --recursive` 和 `make -j4`。本次默认构建 exit=0，链接器 RWX LOAD 警告仍保留，不将其隐藏。不启用 ALWAYS_SWITCH、不加入自动启动目录。

在仓库根目录用 WSL Python 运行：

```sh
python3 scripts/verify-sq1-el2-preparation.py out/virtualization-feasibility-20261031-195101/stages12
```

该检查同时验证完整输入 SHA、八处指令、三个 ARM64 EFI 的 PE subsystem/大小/SHA。失败立即退出；通过仅表示离线材料匹配。

## 独立介质/现场测试门槛（尚未执行）

1. 用户确认现场可手动恢复；先保存 BitLocker 恢复材料（不入 Git）、确认供电、设备身份与现有启动恢复路径。
2. 枚举并确认独立 USB 的物理盘、容量、ESP 标记/UUID；不得写工作站系统盘或现有 Android userdata。目前没有指定测试盘，因此未制作物理介质。
3. 准备经过来源/架构核验的 ARM64 EFI shell 和手动选择的测试文件。EFI shell 尚未选定；不创建 startup.nsh、默认引导覆盖或自动加载 slbounce 的目录。
4. 当前 Secure Boot=true，未签名测试程序不能假定可运行。测试启动信任方案需要单独确认；不自动关闭 Secure Boot，不把运行拒绝判为 EL2 不支持。
5. 用户再次确认后才手动从测试介质运行 sltest，并传入核验后的 TCB 绝对 EFI 路径。成功会显示绿线并**故意停住**，需要手动重启；无绿线/重启/挂起记录为未通过或不确定，不反复盲试。
6. sltest 通过后才准备 SQ1 专用最小 Linux/DT 候选，保留原 EL1 救援路径，验证全 CPU 启动级别、KVM 初始化、/dev/kvm 及最小虚拟机。不能将绿线等同于 Android 终端可用；VSOCK、AVF/Secretkeeper 和外设还需后续验证。

Windows SQ1 的 WSL2 安装尝试连接重置，后续安装命令受工具策略阻止；没有绕过，也没有确认新发行版安装成功。

## 来源

- [slbounce](https://github.com/TravMurav/slbounce)（精确提交见清单）
- [Qualcomm Secure Launch](https://github.com/TravMurav/Qcom-Secure-Launch)
- [qebspil](https://github.com/stephan-gh/qebspil)
- [匹配的微软公共符号](https://msdl.microsoft.com/download/symbols/tcblaunch.pdb/72D3686FBF8403002EE3B786C2B314E62/tcblaunch.pdb)

本分支由现有 SQ1 suspend WIP 分支创建，继承其历史，但本次提交不重启休眠实验，不更改 screen_only/nohibernate、默认镜像或已有设备修复。

## 早期本地准备记录（TCB缺失状态已被上文归档更新）

# SQ1 EL2 preparation — 2026-10-31

Branch: `codex/sq1-el2-preparation-2026103`. Independent worktree; main's
uncommitted work and private device state were not copied or modified.

## Findings

- User reports WSL2 works on Surface Pro X. This supports Windows virtualization
  capability, not a demonstrated EL2 handoff to our Linux loader.
- Earlier read-only SQ2 (.194) evidence found EL1 entry in many saved boot logs,
  KVM/VSOCK disabled and missing Trusty/Secretkeeper dependencies. These are SQ2
  observations, not fresh SQ1 (.248) EL2 measurements.
- [Secure Launch research](https://github.com/TravMurav/Qcom-Secure-Launch)
  describes Qualcomm's signed Windows takeover route. Its detailed analysis uses
  SC7180; applicability to our SC8180X remains to be tested.
- [slbounce](https://github.com/TravMurav/slbounce) provides an EFI implementation
  and isolated `sltest`. Pinned source: c090a8cdafa25e4c99df90f8d6f73f3805d9b397.
  Its DT overlays cover SC7180/SC8280XP/X1E, not SC8180X. Do not substitute another
  chip identity or apply those overlays blindly.

## Offline artifact

`scripts/build-sq1-el2-probe.sh` builds only upstream sltest and records revision,
PE type and SHA256 in ignored `out/sq1-el2-preparation/`. Requires an initialized
pinned upstream checkout, GNU make, ARM64 GNU cross compiler, ELF headers and file.
On Windows-created submodules use Windows Git for metadata checks (a local wrapper
was used); a native Linux checkout avoids drive-letter gitdir incompatibilities.

Build passed: ARM64 PE32+ EFI application,
SHA256 `12e5a3e918c48fb612de9f9ec50f18cadf208090ad91744d4bc5eca1ec0af23b`.
Linker warns about an RWX LOAD segment in the intermediate EFI ELF. This is not a
signature, firmware compatibility, successful takeover, or KVM test.

The pinned test is ACTIVE, not a harmless capability query: it authenticates and
launches TCB code, exits boot services and can halt/reboot/power off. The README's
green-line description differs from test_main.c's final `psci_off()`; do not use
shutdown or a green line alone as proof. Before real tests require explicit
CurrentEL=2 evidence from the executed post-transition path.

## Next gates

1. Read-only SQ1 inventory: firmware version, Secure Boot state, ESP UUID/marker,
   disk identity, active EFI/GRUB and baseline hashes; keep credentials private.
2. Obtain the operator's own Microsoft-signed ARM64 tcblaunch.exe, record version,
   signature/hash and verify the return path expected by this pinned implementation.
   Latest versions may lack it. Do not bundle binaries in Git or fetch random copies.
3. Prepare an isolated/manual EFI test and a dependable original boot route;
   retain screen_only/nohibernate and current661/65Hz working default. No flash,
   normal-boot driver injection, or automatic retry loops.
4. Only after measured EL2 takeover, prepare SC8180X-specific kernel/DT support:
   KVM/VSOCK/VHOST, SMMU/firmware ownership and remoteproc dependencies. Upstream
   calls out remoteproc preboot requirements; audio/charging/GPU must be regressed.
5. Prove a minimal VM first; separately resolve AVF Secretkeeper/attestation before
   claiming Android's Linux Terminal works. Never alter existing lock credentials.

No device was changed/rebooted in this preparation. No working EL2/KVM claim.

## Fresh SQ1 read-only inventory

Firmware reports 4.234.140 (2025-04-30), Toshiba serial/ESP/data/root identity
matches prior SQ1. BOOTAA64.EFI SHA d39ea7f8…; current menu SHA205bde36….
Current cmdline now selects `/boot/ksunext-trial/Image`, with guardY/nohibernate.
This is a newer, externally changed baseline: do not overwrite it using the old
661 deployment menu. No changes to this running trial were made here.
SecureBoot efivar path was absent; state is UNKNOWN, not assumed disabled.
No tcblaunch.exe at the two inspected mounted paths; Windows volumes were not
mounted/scanned, so this is not proof that no local copy exists. Private inventory
is `out/sq1-el2-preparation/sq1-inventory.txt`.

## 2026-10-31 SQ2 (.194) manual experiment staged

User explicitly redirected experiments to SQ2 192.168.8.194 and confirmed onsite
keyboard/screen/recovery readiness. Pulled remote branch
origin/codex/sq1-el2-preparation-20261031 (68b2b4a), merged as da4cb75.
TCB Authenticode check: Valid/Microsoft Windows; pinned hash and eight callback
instruction witnesses passed again. Only pinned sltest SHA12e5a3e9… deployed;
no slbounce driver, DT overlay, new Linux kernel or automatic test request.
Building all three upstream EFI tools here was blocked by incomplete dtc checkout;
that does not affect the already built, hash-matching sltest used in this test.

Fresh target: firmware8.256.140, NVMe serialCYA5N050012105J77,
markerSQ2-INTERNAL-512-20261027, ESP B027-0512, data4f46d2ec…,
root loop0 backing p3, guardY, old boota384f5c1…/Mesa2.7-60.
TCB is from user's SQ1 firmware4.234.140; SQ2 firmware acceptance is untested.
SecureBoot efivars not readable/listed; no trust-state change was made.

Installed /EFI/SQ2EL2Test/{sltest.efi,tcblaunch.exe,SHA256SUMS}, atomically appended
manual menu id sq2-el2-manual before existing entries. Persistent default remains
sq2-baseline60-mesa; timeout temporarily10 for selection. Menu SHA5f69fe5d…,
backup /EFI/BOOT/grub.cfg.before-el2-manual-20261031 SHA2cce07b4….
GRUB chainloader excludes executable from LoadOptions; entry passes dummy
sltest.efi argv0 plus quoted absolute EFI TCB path. GNU-EFI cmdline.c accepts
LoadedImage fallback, so no EFI shell is needed for this invocation.

Source audit correction: trans.s tb_entry executes HVC1, accesses sctlr_el2, then
uses framebuffer to draw a green bar and halt. With test_main.c's nonzero GOP
framebuffer, the green-bar/halt branch occurs before the C psci_off continuation.
Earlier warning that README disagreed with actual test flow was incomplete.
Observed green bar would support single-CPU EL2 takeover in this code path;
it does not prove all CPU EL2/KVM, Linux boot or AVF Terminal availability.

One normal reboot requested after user onsite confirmation. User instructed to
select manual first entry, observe, then long-press off/reboot original Mesa.
Screen result/recovery pending. Do not count SSH loss as successful takeover.
Private preparation/install evidence: out/sq2-el2-test/. No suspend tests.

## SQ2 onsite result: green bar observed

User explicitly reported "顶部出现绿条并停住" after selecting manual sltest.
The exact pinned binary SHA12e5a3e9… sets a GOP framebuffer, enters tb_entry,
executes HVC1, accesses sctlr_el2 and draws green before halting. This is positive
operator-observed evidence for successful single-CPU Secure Launch/EL2 takeover
using the SQ1-extracted signed TCB on SQ2 firmware8.256.140. No numeric CurrentEL
capture was produced, no secondary CPUs started, and no Linux/KVM/AVF was tested.
Do not translate this result into a working Linux Terminal feature claim.

One bounded SSH recovery check timed out after this feedback. User was instructed
to long-press off then boot original Mesa. Recovery/new boot ID remain pending;
no further reboot or experimental launch was requested by the agent. Keep the
manual-only menu and normal/rescue entries; no persistent slbounce driver loaded.
Next gate after recovery is a separate minimal SC8180X EL2 kernel/DT design with
remoteproc/SMMU ownership audited, retaining screen_only/nohibernate and normal
boot. Do not attempt full Android on an unrelated chipset overlay.

## Recovery verified; guest witness prepared

After green-bar test user reports rebooted into Qualcomm version. Readback confirms
new boot049964bb-2a3f-4c33-845e-895ce3e73918, baseline60 Image, vulkan=closed,
boot_completed1, closed Vulkan READY and screen_onlyY. Disk/menu hashes unchanged.
This is a real board reboot/recovery, not a framework restart. Persistent default
remains Mesa; user manually selected Qualcomm. No additional reboot performed.

Added `probe/virtualization/kvm-arm64-witness.c`: static ARM64 test opens KVM,
checks API, creates VM + one vCPU + one host page of RAM, executes four ARM64
instructions and requires an exact 64-bit MMIO write42 at0x100000. No guest disk,
network, Android services or external firmware. SIGALRM limits KVM_RUN to5seconds;
this is not a guarantee against a kernel hang. Build with:

```sh
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -static \
  probe/virtualization/kvm-arm64-witness.c -o /tmp/kvm-arm64-witness
```

Cross-compilation with warnings-as-errors passed. On recovered current kernel it
correctly exits1 with `open /dev/kvm: No such file or directory`; boot ID unchanged.
This validates only current-baseline refusal, NOT guest execution. Guest success
path awaits separately booted KVM-capable EL2 kernel. Do not load/replace default
kernel to make this probe pass without auditing SC8180X DT/hypervisor dependencies.

## Minimal RAM-only EL2/KVM candidate

User requests completion through /dev/kvm verification. Built separate Linux6.3.8
from a COPY of cached sq2-thermal source using tinyconfig + KVM, GICv3, PSCI, EFI
framebuffer, proc/sys/devtmpfs and ARM64 ELF/script support. Suspend screen-only
patch remains default-on; nohibernate in commandline. No BLOCK/NVMe, GPU, remoteproc,
USB, QCOM SCM/SMMU drivers in this minimal config. No Android or persistent writes
from test init. maxcpus=1; secondary-CPU behavior remains untested.

Added source files: build-sq2-el2-kvm-kernel.sh, prepare-sq2-el2-kvm-ram.py,
el2-launcher-main.c and el2-kvm-init. Inputs are ignored out/el2-kvm/{busybox,base.dtb}
and previously built witness; source DTB fixed6db562c2… belongs SQ2, all GPU/zap,
remoteproc/USB/PCIe matching paths disabled in a separate copy, identity/reserved
memory retained. This is deliberately not a general SC8180X Android overlay.
Launcher combines pinned slbounce code with an EFI application loading our minimal
kernel. It installs the test DTB explicitly, checks slbounce's DT gate, then installs
SL hook. It does not load an EFI runtime driver into ordinary boot.

QEMU virt+EL2/GICv3 test of exact Image/initramfs: All CPUs started at EL2,
KVM VHE initialized, /dev/kvm10:232, guest MMIO42 PASS, KVM_WITNESS_EXIT=0.
QEMU terminated after35seconds with timeout while init's90second result wait was
active. This validates test software, not SQ2 hardware.

Candidate Image1a6c86d9…, DTB567a254a…, initramfs63100b9e…,
TCB195997cf… staged under EFI/SQ2EL2KVM. Menu f7244134… adds manual first entry;
original persistent Mesa default untouched, backup before-el2-kvm-20261031 is
5f69fe5d…. One normal reboot/manual selection failed before Linux with
/loader/efi/chainloader.c88 unknown error. User recovered; bootfaf4eb80… guardY.
Cause unconfirmed; GRUB devicetree command need not install FDT for a chainloaded
app, so v2 explicitly installs its own validated FDT before checking the gate.
V2 launcher4efc6b7b… adds20second stage/status diagnostics. Installed atomic EFI
replacement and all five manifest hashes passed (initial CRLF manifest corrected).
Second normal reboot/manual selection requested; result pending. No /dev/kvm
hardware-success claim yet. Physical disk identity/UUID/root were checked before
installation; no suspend tests. Raw evidence out/el2-kvm/.

## Hardware /dev/kvm gate passed (operator observation)

User explicitly reports for v2 manual SQ2 RAM test: "出现 /dev/kvm 且 PASS、EXIT=0".
This meets the requested hardware gate: on the isolated EL2 Linux6.3.8 test,
/dev/kvm was created and the single ARM64 vCPU executed the exact MMIO42 guest
witness with exit0. This result is operator-observed screen output, not an SSH
capture of the temporary RAM system. The init intentionally provides no network
or persistent disk writes. QEMU success is separate supporting software validation.

A fresh SSH recovery check after receiving the report timed out. Recovery after
this reported test is pending; the earlier bd429ef0 normal boot cannot be assigned
to this test without chronology evidence. Candidate init waits90seconds then calls
reboot; if it remains stopped, manual power-cycle/original boot is required.

Scope: single CPU/maxcpus1, tiny kernel, no disks/Android/GPU/audio/remoteproc/USB.
No evidence yet for SMP, normal Android kernel compatibility, full AVF Linux
Terminal, VSOCK/Secretkeeper/attestation or peripheral preservation in EL2.
Normal persistent Mesa default and rescue remain. No suspend or repeat tests.

Recovery after reported hardware PASS is now confirmed by user and SSH:
bootffa7053f-ec2c-4c0b-94b9-1e866b7fed3a, original baseline60 Mesa2.7-60,
boot_completed1, screen_onlyY/nohibernate. Menu f7244134… and persistent default
sq2-baseline60-mesa unchanged. No extra reboot or experiment. Evidence:
out/el2-kvm/after-hardware-pass-recovery.txt. /dev/kvm requested gate and subsequent
normal-system recovery both completed; automatic-vs-manual recovery not inferred.

### Photo correction and PCIe diagnostic follow-up

User initially selected NVMe-present answer, then supplied a clear photo and
clarified no NVMe logs. Photo confirms HOST_CPUS8, all CPUs started at EL2,
KVM VHE and guest MMIO42 PASS/EXIT0, BUT `ls: /dev/nvme*: No such file or directory`
at about10.6s. NVMe enumeration is NOT passed. Original photo saved privately in
out/el2-android/fullram/operator-screen-nvme-missing.jpg. Never interpret the initial
multiple-choice answer as stronger than the supplied direct evidence.
PCIe/QMP PHY/GCC/RPMH/interconnect/NVMe dependencies are built-in, not missing
modules. Normal recovered boot6406bb24… has1c18000.pci bound toqcom-pcie and
0002:00:00.0/01:00.0. Experiment cause remains unknown (early snapshot, deferral,
EL2 PCIe ownership or other failure). Full Android payload not launched yet.
Updated RAM initramfs f19ebf18… waits up to45s, suppresses kernel-console mixing,
prints PCIe/NVMe/deferred/platform/PCI bus diagnostics and KVM result, waits120s
then reboots. All hashes passed; third normal reboot/manual fullram selection
requested. No disks mounted by test. Waiting for diagnostic screen result.

User now authorizes one-shot GRUB diagnostics without manual selection in future.
Prepared consume-before-load generator, none armed yet; install after normal
recovery, keep persistent default and enter candidate only if save_env succeeds.

## Full Android kernel / terminal preparation (not boot-tested yet)

Built separate full Android6.3.8 Image/modules using a COPY of the prepared
sq2-ksunext source (existing memfd/audio/Bluetooth/screen-only fixes retained).
KSU explicitly disabled for this baseline test. Enabled KVM/PREEMPT_NOTIFIERS,
VSOCKETS and VHOST_VSOCK; original NVMe/FUSE/thermal paths retained. Full modules
build produced Module.symvers. Initial build failed because cpio was missing for
IKHEADERS; installed cpio then full build passed. A copied KSU default initially
enabled itself, corrected before successful build. No such intermediate boot.

GPU6.3 code did not honor disabled zap-shader status; experimental source now
returns ENODEV for an unavailable child, allowing the existing non-zap path.
This does not change normal enabled-zap behavior. Full RAM SMP8 QEMU passed;
physical full RAM user/photo confirms8 CPUs/allEL2/KVM guestPASS but NVMe missing.
Do NOT mark complete Android boot or PCIe/NVMe working.

Offline full Android initramfs175744446B assembled from all SIX currently deployed
baseline+BT+touch addons (preserving current per-device calibration/userdata),
replacing331 modules with same-build stripped modules. Battery adapter rebuilt
against full symvers, TSENS separately rebuilt against same full kernel headers.
Original non-video effective modules all replaced; old experimental Venus/native
modules retained but candidate must pass sq2.video=0 and no experimental-video
flag to prevent loading incompatible modules. Rebuilt subset module dependency
metadata; effective-entry comparison passed; original SSH credential member last.

Experimental el2-terminal-stage requires guardY,/dev/kvm,/dev/vhost-vsock and RAM
vendor overlay; hides only the unavailable Trusty Secretkeeper VINTF declaration,
then runs KVM witness. /data and credentials untouched. Experimental build.prop
advertises protected_vm.supported=0 (only ordinary KVM). This is not hardware-
backed security support. Existing terminal Debian payload is already present in
user10 files/linux, but AVF and terminal have NOT been started on new kernel.
Candidate is still LOCAL ONLY until PCIe/NVMe diagnostic gate passes.

## One-shot diagnostics installed

User requested future diagnostics without manual selection. Generator
scripts/prepare-sq2-el2-once.py creates1024B GRUB env and appends whitelist-only
consume-before-load block; candidate selected only if save_env clears request.
Persistent default remains baseline60 Mesa. Installed after verifying normal boot
f091ffcd…/diskUUIDs/root; menu6ef2e1f7…, backup before-el2-once-20261031 is
da0497db…. Requestfullram env92407f5d… armed once. Updated diagnostic initramfs
e743de2a… includes missing tail applet and prints SMMU + supplier driver links.
Old photo showed PCIe1c18000 deferred, noPCI bus, noNVMe, and missingtail; thus no
PCI/SMMU failure diagnosis can be inferred from that missing log pipeline.
New normal reboot requested, automatic test screen/consumption result pending.
No Android test candidate launched, no suspend test or default promotion.

## SMMU mismatch diagnosed and bounded pool candidate passed RAM test

New photo from automatic diagnostic identifies apps SMMU15000000:127 context
banks but only106 DT context IRQs, driver refuses probe; PCIe1c18000 waits for it.
This explains PCI/NVMe absence at the driver-dependency level. Normal kernel log
ring no longer contains early SMMU messages; fresh normal inventory/dmesg saved
privately, not treated as EL2 logs. Normal recovery6922ba24… and once=none verified.

User requests skipping additional boundary tests to speed lab experiments; complied
(no new offline boundary-test suite). Added opt-in patch/helper for EXACT SC8180X
apps SMMU15000000, EL2,127CB/106IRQ/0S2-only. Physical bank count/reset remains127;
bitmap reserves106..126 permanently, domain allocation uses only IRQ-backed0..105;
num_context_irqs never expanded beyond allocated IRQ array. Standard strict failure
remains unless experimentalDT property present. IOMMU translation remains enabled;
no fabricated IRQs, identity bypass or chip substitution. Wiring/fault-path behavior
beyond the present successful workload remains unverified.

New Image8ded0238…/DTB41217ae3… installed only in fullRAM test directory with
previous files backed up. Kernel build passed; same-config modules unchanged ABI.
One-shot fullram requested and rebooted. User reports PCI and NVMe now both appear,
KVM remains PASS. This is operator confirmation; actual rootfs reads and Android
boot not yet tested. Separate Android candidate uses same kernel/DT and matched
modules; preparing it while RAM test waits120s then returns to default.

## Android one-shot payload ready

After RAM NVMe success normal recovery94a64dbd… guardY/completed1 confirmed;
no additional manual confirmation required for already authorized lab continuation.
Prepared EFI/SQ2EL2Android with Image8ded0238… + scoped DTB41217ae3… and complete
Android initramfs. Matched modules331, same config; previous current BT/touch addon
files and credential member preserved. Candidate commandline sq2.video=0,
sq2.el2=experimental, Mesa2.7-60, nohibernate/screen_only and saved WiFi flow.
KVM stage requires video off to keep old experimental Venus modules unloaded.

Found old boot-kmsg forks and ignores SIGTERM, so wrapping timeout would NOT bound
logging. Fixed before deployment: new foreground collector caps180s/8MiB, caller
backgrounds it; no fork, no ignored termination signal. Candidate-only ESP logging,
not normal boot. Final initramfs180495815B (level3 gzip); raw pack-audit.json tracks
all replacements. Static ARM64 collector compiled -Werror. No extra boundary test
suite per user instruction; syntax, build, file/hash/credential checks retained.
Android candidate not yet booted at this record; first-boot result still pending.

## Android candidate desktop confirmed by operator

The one-shot Android candidate was installed and a normal reboot requested;
persistent default remains sq2-baseline60-mesa. Operator reports desktop reached,
touch and Wi-Fi normal. This is the first full Android candidate boot report,
not yet an SSH verification of the running kernel or AVF guest execution.
Repeated SSH attempts to the previous address192.168.8.194 timed out and the
workstation neighbor entry is unreachable. Current device IP was requested;
do not infer a kernel crash or working Internet access from these observations.
Actual boot ID, KVM/VSOCK nodes, bounded boot logs and Linux Terminal shell
execution remain pending. No default promotion or additional reboot performed.

## 2026-11-01 correction: feature parity required; Wi-Fi failed

Operator corrected the earlier Wi-Fi report: the device was not initialized.
Desktop/touch confirmation stands; Wi-Fi did NOT pass. User requires existing
hardware decoding and KernelSU Next support in the EL2 integration. Hardware
decoding is a completed normal-build feature (see BUILD/HANDOFF); historical
`experimental` switch names do not justify removing it. The initial Android
candidate's video=0 and disabled KSU are not acceptable for feature parity.

Operator rebooted to original default. SSH confirms recovery boot
3ff3e94d-abe9-4a57-b3ff-a6c7b2860cf4, baseline60 Mesa, guardY/nohibernate,
boot_completed1, wlan0 and /dev/video0 present; one-shot request consumed (none).
No new experiment armed. Retrieved prior candidate logs from ESP, privately under
out/el2-android/failed-boot-50eb/googlebook-native-50ebcb47-2290-4dbc-998c-2a1f9a808da6.
They independently confirm all CPUs EL2, VHE initialized, and the actual Android
early-stage KVM witness PASS. They also show ADSP/CDSP PAS image initialization
returning -22 and ath10k without completed initialization. Wi-Fi root cause is
not yet isolated; donor6.12 module load failures also occur and must not alone
be assumed causal without baseline comparison.

Pinned slbounce README points to qebspil for EL1 remoteproc preboot before EL2.
Local qebspil8e4d9e6 has no SC8180X table entries; Linux also needs preboot takeover
support. This is a candidate direction, not an installed or verified fix.
Venus PAS startup must be covered too; merely re-enabling video flags is inadequate.

Kernel builder now enables KSU/KPROBES/KSU_KPROBES_HOOK and the normal narrow
media foundation, preserving USB audio's lack of media-controller coupling.
KSU-only intermediate build passed (v3.2.0-legacy/33129); media-inclusive Image
and modules also built successfully, Image SHA256
da4e804f4b61adbcb801821aec1589ed7f9159b0b3be00f721a8b3afc981c23c.
Final custom Venus/power/overlay module payload remains pending. Outputs use
out/el2-android/parity-v2; no replacement Image/initramfs has been deployed.
Remaining: complete firmware takeover, matching native video modules, retain
all effective normal payloads, then test Wi-Fi/audio/video/KSU and Terminal.

## 2026-11-01 parity continuation: RMTFS failure and preboot candidate

Recovery rechecked: same3ff3e94d boot, guardY, wlan0/video0, no /dev/kvm.
Normal video helper reports SQ2_NATIVE_VIDEO_READY; no new playback claim.
Failed EL2 log has qcom_rmtfs_mem85500000 assign-memory failure(-22); rmtfs then
cannot open its device and mmap fallback fails. Thus wifi-services never reaches
the normal modem startup. Normal boot has /dev/qcom_rmtfs_mem1, all three
remoteprocs running and active RMTFS requests. This adds a concrete missing
dependency; it does not prove a single fix will restore Wi-Fi.

Current Venus sources regenerated using prepare-sq2-venus-iris1.py with
--firmware-lease --platform-entry --sq2-resources into the EL2 build's venus
directory. Built against the complete media/KSU/KVM Module.symvers without
KBUILD_MODPOST_WARN. Core9d4638ab…/decoder e8b0e478…; module build passed,
with an existing unused-variable warning. Custom overlay modules and final
initramfs still need rebuilding/audit, so the old video-disabled packer is NOT
a ready parity-image installer.

Added prepare-sq2-el2-preboot.py, an isolated qebspil preparation script pinned
to8e4d9e676a3b3afe136cda9b953a2139ff1a32d0 and exact dtc/gnu-efi revisions.
Adds SC8180X ADSP1/MPSS4/CDSP18 PAS IDs taken from the actual6.3 driver.
Requires Surface root compatible, explicit microsoft,el2-preboot and existing
zero-valued microsoft,el2-preboot-complete DT property, plus broken-reset.
Sets completion1 only after firmware authentication/start succeed; failed DSP
status is not hidden behind another DSP's success. No proxy vote supplied for
these untested SC8180X entries; availability/power sequencing remains unverified.
Reduces upstream's500-second failure stall to5seconds for this candidate.
Build passed, AArch64 EFI e27e3cb9834c7c7242a53a31a5092fed0e17a1bdd171769a42f16a7f9661bf31.
Artifacts private in out/el2-android/parity-v2, stage /root/.cache/sq2-el2-preboot-20261101.
No device write/reboot, firmware copy or preboot driver load in this turn.

Next required work: matching Linux attach implementation with success-marker
checks and recovery disabled for non-restartable firmware; RMTFS memory ownership
handoff; VPU PAS/firmware lifecycle under EL2; rebuild custom video overlays and
complete parity payload, then one-shot hardware/Terminal validation. Merely
setting broken-reset or skipping failed SCM calls is not a verified handoff.
Upstream references: qebspil README; maintainer's remoteproc attach-only recovery
discussion https://www.mail-archive.com/linux-remoteproc@vger.kernel.org/msg00793.html.
The older codelinaro raw reference URL redirected to login HTML, not usable C
source. No code was copied from that failed download.

## 2026-11-01 preboot handoff and ESP cleanup

Normal system recovered at f7f98e82; after Wi-Fi reconnect SSH returned at .194.
The normal kernel/initramfs, firmware hashes, screen_only=Y and video helper
readiness were captured under private out/el2-android/recovered-f7f98e82.
No fresh decoded-frame or listening result is claimed.

Implemented an opt-in EFI/kernel handoff, built in the isolated EL2 tree:

- qebspil SC8180X PAS1/4/18 startup plus RMTFS HYP assignment for the exact
  0x85500000/2MiB/client1/VMID15 region. Result cells start at zero and change
  only after successful calls. Retries do not reset an already started DSP.
- Kernel remoteproc attachment requires the matching Surface/EL2/result1
  properties; no firmware remapping/reload or unsupported reset recovery.
  Normal remoteproc behavior remains unchanged without the explicit opt-in.
- RMTFS uses the verified preboot assignment and avoids EL2 ownership teardown.
- Linux6.3 EFI stub copies the DT before late EBS. Copy the four result cells
  from the original DT after successful EBS, using no EFI boot-service calls.
  The original DT is launcher-allocated EfiLoaderData, not freed by qebspil.

Image fe6207e96f3ca11c51690a5deee0c887c6a6b8633b4f4488a2977a5f6614fed9
includes KSU Next33129/v3.2.0-legacy kprobes, media foundation and KVM.
qebspil r3 fac3ac8d46a4c88ef7c2d0c68d1c50979e6886e5a597a4215b3a304a34ac68b4.
These are experimental handoff artifacts, not proof of restored Wi-Fi/audio.

Added a separate preboot RAM diagnostic and `preboot` consume-once request.
Only the identified ESP is mounted for bounded logs; no Android/userdata,
association, DHCP or credentials. Firmware bytes match the actual normal system.
The first test auto-rebooted without saved logs. QEMU reproduced a KSU crash
because the diagnostic command line omitted `lsm=selinux` (CONFIG_LSM excludes
SELinux by default). Adding normal-system `lsm=selinux enforcing=0` allows
the same Image to boot eight host CPUs and execute the KVM MMIO42 guest, EXIT0.
This explains a reproducible diagnostic flaw; no physical panic trace was saved.
The v2 launcher e35b1d47… and initramfs5077299c… were installed and armed after
cleanup. They print ESP_LOG and retain the result screen for180seconds, with
a360second userspace watchdog. Firmware/kernel hangs still need manual recovery.

Custom video module ABI rebuilds now accept an explicit kernel/output/base-DTB.
Both regenerated overlays were byte-compared with the embedded deployed overlay
before strict modpost builds (no MODPOST_WARN): identify18dbce98…, HFI38c74c2b…,
software-domain VIDEOCC24ed5d48…. Their register/clock/domain policy was retained.
Venus core/decoder also built earlier against this media/KSU ABI. No new video
module has been deployed; VPU PAS/firmware lifetime under EL2 remains unresolved.
The old video-disabled Android packer is still not a feature-parity installer.

User requested ESP cleanup. Before deletion, the entire ESP was archived locally
as private out/esp-cleanup-20261101/esp-before.tar, SHA256
7d4570756ba652c221be3285d13714243fac72dc2fdeae2535788957b09deedc, verified against
the device archive. Retired2715 files (1,576,345,259 file bytes): obsolete android1,
dual65 and stability builds, failed EL2 Android remainder, historical logs/menu
backups/env switches, and redundant FullRAM/old touch addon backups. Kept hashes
were checked before and after. ESP now422.9MiB used / about1.6GiB available (21%).

Current menu SHA3e63f6a8829178d91742532f08cfa2eb1cb9b8781992cb4f19accddeeb382524:
Mesa default, Qualcomm, Rescue, preboot diagnostic, FullRAM, tiny KVM, manual
Secure Launch and shutdown. Normal/rescue image, DTBs, credential-bearing base
and effective touch/Bluetooth addons remain byte-identical. SQ2Buttons loader
is retained; its obsolete menu now delegates to the current main menu. Identity
markers, sq2-port metadata and preboot firmware remain. No partition/userdata
changes. Old on-ESP menus are no longer recovery instructions; use the local
archive if a retired experimental artifact is actually needed.

Preboot v2 hardware result saved under el2-preboot-808f9035-89b8-4530-910c-8aa440d188d1:
HOST_CPUS8, NVMe present, KVM guest MMIO42/EXIT0, ESP_LOG=SAVED. All four
completion cells were0, no remoteprocs attached and no wlan0. Normal default
returned at dc2fcd9d with wlan0 and screen_onlyY. Module-load exit0 alone is not
a device probe result (the driver explicitly rejects absent completion markers).

Review then found slbounce flushes caches before invoking real EBS; our late
callback changed result cells afterwards without a second writeback. Therefore
the zero cells cannot distinguish failed startup from lost result writes.
qebspil r4 now flushes the result DT after every stage update. A six-cell trace
records stage, EFI status low/high, and low32 of SCM results0..2; the kernel
EFI stub copies this trace to its final DT alongside the strict completion bit.
Stage1=RMTFS prepared,2=calling SCM,3=returned,4=assigned; PAS10=starting,
11=success,12=failure. This is diagnostic evidence, not an override of success.
v3 Image96929547…, qebspild40b4efa…, DTBb6432781…, initramfs8d68c06d… deployed
and armed once, with the same normal menu/default. Normal diagnostic exit now
unbinds logs and unmounts the ESP before reboot; the timeout fallback remains
bounded. No Android parity candidate deployed.

User photo IMG_0421.JPG captured v3 before Linux: EFI RMTFS returned
status=Success, result=1/0/0, then our wrapper incorrectly returned Device Error.
The pinned qebspil scm_sip_interface.h explicitly associates
HYP_MEM_PROTECT_ASSIGN with tz_common_rsp_t: its first INT64 is the completion
header (1=completed, 0=not completed), and the next INT64 is ret (0=success).
It is not the Linux qcom_scm_res layout. Cross-checked the published ABL caller:
https://github.com/SHIFTPHONES/android_bootable_bootloader_edk2/blob/7d391d8b1059ed48259728cab004ca6f16ee3de2/QcomModulePkg/Library/avb/libavb/avb_util.c
ScmGetFeatureVersion checks common_rsp.status==1 before consuming the value.
No firmware error is being ignored: r5 now requires EFI success, completed=1
AND extended ret=0. A host harness compiled the actual generated wrapper and
its real response structs: six cases cover successful completion, incomplete,
negative/positive extended errors, invalid header and EFI transport failure.
All passed, and the assignment arguments/region/permissions are unchanged.

User manually recovered v3 to normal boot519057aa, wlan0 .194, screen_onlyY.
No v3 Linux log directory exists. r5 EFI built as AArch64, SHA256
ce40736235dd11e23b61cb0bcaad983e89313848c31e21687635b3f5a97fef7a.
Preboot v4 changes only this EFI plus its checksum manifest; Image/initramfs/
DTB/launcher remain v3 bytes. Added cold reset after a 5-second RMTFS/PAS error
display; this cannot recover a hung firmware call. Installed after identity and
old/new hash checks, armed consume-once. Plain initramfs `reboot` returned0 but
boot ID remained519057aa and request stayedpreboot; it was not a board reboot.
Use Android setprop sys.powerctl reboot for the actual test. Hardware result
still pending; this correction alone is not Wi-Fi or Terminal validation.

The next user photo (clipboard55050943) confirms v4 RMTFS assignment Success,
then Starting remoteproc for MPSS, CDSP and ADSP. No Linux console followed.
Progressing to later DSPs shows earlier pil_finish calls returned EFI success;
the final ADSP Starting line does not distinguish an in-call hang from a later
EBS/Secure Launch/cache handoff failure. Do not claim ADSP auth failed from it.
Device .194 remained unreachable; requested manual recovery to original default.

Prepared v5 locally (not yet deployed): qebspil r6 brackets PAS init/mem_setup/
start calls and prints the aggregate completion. The isolated launcher captures
GOP before EBS and writes a small raw framebuffer hex marker with no EFI calls
after EBS: 10=enter EBS,11=EBS returned with status,20=enter SL_AUTH,
21=AUTH returned with result,30=enter SL_LAUNCH,31=returned with CurrentEL.
The next16 hex digits are the value. No firmware/start ordering or cache-flush
policy changed. Image/initramfs/DTB/TCB hashes remain identical to v3/v4.
qebspil84a9ae1a9953f4fa7d92cb40f470549bca450d44d2adf01cfb99b8205945578d;
launcher5a23d8cd0fcb5c9c8b4329b4603996d66acaa1f7987d6a5f411e0d305f84913e.
Build and Python syntax passed; upstream linker RWX/WSL clock-skew warnings
remain. Linux/Android/terminal functionality still unverified for this path.

User restored normal system at boot6c0e84de-f908-4b27-b66e-84612671eb28.
Verified normal Mesa cmdline, screen_onlyY, exact NVMe/ESP/root identity and
unchanged main-menu hash3e63f6a8; consume-once was none and no v4 Linux logs
existed. Installed only v5 qebspil/launcher plus manifest after old/new checksum
checks, armed preboot once, and invoked Android sys.powerctl reboot. v5 result
pending. The framebuffer renderer passed an ASan/UBSan host harness for all
hex digits, null framebuffer and preservation outside its480x40 pixel band.

v5 operator result: aggregate PAS finish returned Success; framebuffer stage31
with value2. Thus the Secure Launch return path actually reached EL2 after PAS
calls, but Linux did not reach its console/log collector. This is not evidence
that Linux attached remoteprocs or that Wi-Fi/Terminal works. Normal restored at
boot62ff95c5-a189-46dc-b943-aaba49f104ed; no v5 Linux log directory exists.

v6 candidate adds a second cache sweep after successful real EBS (including
late DSP callbacks), before SL_AUTH. It reuses the exact upstream descriptor
allowlist; reserved/conventional/MMIO regions are not swept. Original pre-EBS
sweep is retained, AUTH/LAUNCH order and DSP firmware unchanged. This tests
the hypothesis that callback-dirtied state was lost during Secure Launch;
stage31 alone does not establish this root cause. Added12/13 around the new
sweep. Only launcher changed to
feb4aca41a9818b680596562e7799ec6af7c7560d4f6a77023c088dca02ee69f.
Built successfully (same upstream RWX/WSL timestamp warnings), inspected order
and unchanged other payload hashes. Installed/armed once after identity and
hash checks, reboot requested via Android sys.powerctl. v6 hardware pending.

v6 user result remains31/value2, so the extra cache sweep did not resolve the
stall. Normal recovered at boot53c9d620-c8f6-4d3d-8ad0-b558038777e1.
The RAM launcher had loglevel4, hiding early informational kernel output;
stage31 alone cannot locate a stall inside the EFI stub versus early kernel.
v7 adds explicit sq2.el2_trace=1 diagnostic markers in the Linux EFI stub:
40=efi_exit_boot_services returned/status,41=before copying late DT results,
42=copy finished/new DT address,50=about to enter kernel/kernel address.
The ARM64 existing screen_info is captured before EBS; raw pixel writes reuse
the bounded launcher renderer. No EFI console/services are called afterwards.
Without the Surface+trace opt-in, the helper stays inactive. Verbose loglevel7,
ignore_loglevel and initcall_debug expose subsequent early kernel progress.
Full kernel Image build passed, preserving KSU33129/media configuration; only
EFI stub code was changed, initramfs/modules/DTB/firmware byte hashes unchanged.
Image d143442af5dfc46409e1ad82bd8f42590930e88531271748ac90126917e71c8f;
launcher5dbfa7551efd699eaddeeca10cbd5b6ccd666bf92e251a7f0c68b178da30b12a.
Installed these two diagnostic files and manifest after identity/hash checks;
armed once and requested Android reboot. v7 hardware result pending.

v7 hardware result: user reported `41 F81F100`. This brackets the stall inside
the post-EBS result-copy helper; it does not identify the exact instruction.
v8 removes that helper: before EBS the stub writes its final DT address into
the explicit 8-byte `microsoft,el2-kernel-fdt` property. At EL1 after successful
PAS completion, qebspil validates all four source results and destination
fields, copies completion/status in place and flushes the destination cache.
No failed/missing PAS is promoted to success. Non-opt-in normal DTs are ignored
by the stub; malformed opted-in handoffs fail. Stage42 now means EFI returned
successfully without post-EBS result parsing; stage50 precedes kernel entry.
Host tests using the actual diagnostic DT and pinned libfdt passed result/trace
copy, callback retry and failure rejection with ASan/UBSan. This caught a
4-byte-aligned DT u64 access; the publisher now uses fdt64_ld. Kernel/EFI build
passed (existing KSU version fallback and upstream linker warnings remain).
User restored normal boot707c599b-a9e2-4769-a481-100a6f58e320. Serial/ESP marker,
UUID/root backing, screen-only guard, unchanged main menu and old/new hashes
checked. Only diagnostic Image/DTB/qebspil replaced atomically, then one-shot
armed and Android sys.powerctl reboot requested. v8 hardware results pending.
Image: 975777d33002d6a251423d00ffd58d7c05757f25d459443db8979f2fe084b15b
DTB: f0fead9f9395ac668a8f838665635e3db8dc736d2e0bba5ac1c58fba9e3fc6f1
qebspil: 5e2adb865235cc73c98acf1c12656d2a1582d3f60c81664f19d68ba60120e785
Artifacts/deployment and private host harness: out/el2-android/preboot-ram-v8
and out/el2-android/test-preboot-publish.py. No Terminal/Wi-Fi parity claim yet.

v8 recovered automatically to normal boot955804b6-1cd3-492c-bda1-91d7bf639545.
Retrieved ESP logs for diagnostic27e329d1-075c-49b6-9c36-c09ba24ab58a under
out/el2-android/preboot-ram-v8. All four completion values1; three DSPs attached,
8 host CPUs, NVMe and RMTFS devices present, actual KVM MMIO42 witness EXIT0.
No wlan0. ath10k probe eventually returns0 despite early dependency-load noise.
pd-mapper reports no maps; kernel remoteproc_sysfs.c exposes "unknown" for every
RPROC_ATTACHED. pd-mapper discovers JSON directories from remoteproc/firmware
(upstream reference https://github.com/linux-msm/pd-mapper/blob/master/pd-mapper.c).
v9 scopes known firmware exposure to the verified Surface/EL2 three-PAS handoff,
done1, explicit opt-in and exact rproc/DT filename match. Other externally booted
images remain unknown. Added firmware paths, rpmsg binding, QRTR socket, process,
module and interrupt logs. Build and shell/Python checks passed; only diagnostic
Image/initramfs atomically replaced after identity/old-new manifest checks.
v9 Image13e4d1cb7aec491fd56acdca4150ca66ba34cdd7658437729f19c1fae05f1bd6;
initramfs1f3887d42946aa096023e240c3bd5ac7fb448c50edc92442252d43f532b1a718.
One-shot armed and reboot requested; automated SSH recovery observation running.

Full Android packer now requires a fresh explicit output directory, no longer
skips native video modules, selects rebuilt custom Iris/HFI/VIDEOCC/Venus over
generic substitutes, checks KSU/KVM config, and preserves video userspace and
firmware bytes. Terminal stage rejects video=0. Local parity-pack-v1 builds342
modules including11 video modules and retains the last credential member.
This is payload preparation only; it was not deployed. Its Image association
predates v9's sysfs fix and must be regenerated for the next Android candidate.
VPU PAS lifecycle under EL2, actual hardware decoding and Android Terminal
remain incomplete; no claim of full feature parity from the packaging audit.

v9 auto-recovered to4938b520-e8e6-49fe-ad1e-f7d6f199cbb2. Log directory
el2-preboot-a88ad43e-0271-4e8f-9669-592a9ddd0966 retrieved under preboot-ram-v9.
All firmware paths now correct; pd-mapper remains alive without "no maps".
Wi-Fi still absent, KVM still PASS. ADSP/CDSP IPCRTR bind and GLINK IRQs advance;
modem has only rpmsg_ctrl, no IPCRTR, and GIC481 GLINK count0. Thus the metadata
fix is confirmed but insufficient. No proof that all DSP services are running.

v10 adds a modem-only DT opt-in proxy resource test using the existing
adsp_start PD/XO/aggre2/regulator requests. It logs bounded SMEM421 contents,
unwinds acquired resources on error, performs no PAS call/reset/reload, and
holds successful votes only for the diagnostic lifetime until reboot. The
matching normal resource voltage/performance policy is reused. This temporary
hold is not a completed handover or battery/power-management implementation.
Rebuilt Image/modules; repeated make after clock skew warning finished cleanly
apart from existing KSU version messages. Every packaged module was compared
bytewise after strip against the recheck build, then the new Image copied.
Image a384f89903813b52a1097533ba9853bca8c4e5cb5f9658f33b259c8e65e43e00.
Identity/menu/old-new manifest checks passed; only diagnostic Image/initramfs/DTB
replaced, request armed and Android reboot issued. Recovery monitor started;
v10 result pending. Private deploy files under out/el2-android/preboot-ram-v10.

Added build-only prepare-sq2-el2-android-preboot.py: checks RAM bundle manifest
and paired Android audit/kernel hash, preserves video flags, copies EL1 preboot
and EL2 handoff launcher, and strips the RAM-only modem proxy-test DT property.
parity-pack-v2 plus android-preboot-v1 built against v9 Image13e4d1cb; no device
deployment. Payload hash9245cef34c73784d7c841c51c1c7dafadf22fca0603e814e012c3045ab7b27f9.
Still requires actual modem/Wi-Fi, video/VPU lifecycle, KSU and Terminal runtime
validation. These bundles must not replace the normal default on packaging
evidence alone.

2026-11-01 snapshot follow-up: v10 automatically recovered to normal boot
d0c85809-6840-4ef1-ac4f-6b63f26c8011. Retrieved diagnostic logs for
ae90d5ad-3db1-479b-a07f-d528aaeb37c6 confirm proxy resources were held, but
modem IPCRTR/Wi-Fi remained absent and KVM witness still passed. The proxy
experiment did not fix the problem. Its RAM-only opt-in remains in this source
snapshot; remove it before the next diagnostic. The Android builder strips it.

Added el2-rmtfs-start and its isolated host test: normal boot retains rmtfs -s;
the explicitly selected EL2 path requires the expected attached modem, firmware
name and RMTFS character device, then omits -s to avoid restarting that modem.
The host cases passed; the latest helper/packer changes have not been repacked
or deployed. Existing android-preboot-v1 therefore does not contain this helper.

Inspection also found that pinned qebspil PAS wrappers discard SCM Results and
return only the EFI transport status. Previous PAS Success/completion flags
must not be treated as validation of the PAS response payload. RMTFS has its
own checked response handling; equivalent PAS response auditing is pending.
No PAS fix has yet been implemented. Real Wi-Fi, hardware decoding, KSU and
Android Linux Terminal acceptance remain outstanding.
