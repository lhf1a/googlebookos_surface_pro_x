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
