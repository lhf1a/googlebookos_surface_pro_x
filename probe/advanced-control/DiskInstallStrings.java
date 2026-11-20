package org.surface.advancedcontrol;

import java.util.Locale;

/** Bilingual CN/EN strings for the Internal Disk Fresh Installer. */
public final class DiskInstallStrings {
    private final boolean zh;

    public DiskInstallStrings(Locale locale) {
        this.zh = locale != null && "zh".equalsIgnoreCase(locale.getLanguage());
    }

    private String t(String cn, String en) {
        return zh ? cn : en;
    }

    public String title() {
        return t("写入内置硬盘", "Install to Internal Disk");
    }

    public String homeSubtitle() {
        return t("从 USB 启动时将当前系统全新安装到内置硬盘", "Clean-install running USB system to internal SSD");
    }

    public String pageSubtitle() {
        return t("将当前 USB 启动的系统与引导载荷全新写入内置硬盘，不保留原内置硬盘数据，也不迁移 U 盘用户数据。",
                "Clean-install the running USB system and bootloader onto the internal disk without preserving existing internal data or migrating USB userdata.");
    }

    public String backDesc() {
        return t("返回", "Back");
    }

    public String refreshDesc() {
        return t("刷新磁盘状态", "Refresh disk status");
    }

    public String cancel() {
        return t("取消", "Cancel");
    }

    public String bridgeMissingBanner() {
        return t("未检测到系统安装桥接服务，当前仅可查看状态。",
                "System installer bridge service not detected.");
    }

    public String loadingStatus() {
        return t("正在检测启动介质与内置硬盘…", "Detecting boot media and internal disks…");
    }

    public String bootSourceSection() {
        return t("当前启动环境", "Current boot environment");
    }

    public String bootSourceUsb(String disk) {
        return t("已从 USB 外置磁盘启动 (" + (disk.isEmpty() ? "USB" : disk) + ")",
                "Booted from external USB disk (" + (disk.isEmpty() ? "USB" : disk) + ")");
    }

    public String bootSourceInternal(String disk, String transport) {
        String kind = "nvme".equalsIgnoreCase(transport) ? "NVMe" : (transport.isEmpty() ? t("内置存储", "internal") : transport.toUpperCase(Locale.US));
        return t("当前已从内置硬盘运行 (" + kind + (disk.isEmpty() ? "" : " · " + disk) + ")",
                "Already running from internal storage (" + kind + (disk.isEmpty() ? "" : " · " + disk) + ")");
    }

    public String bootSourceUnknown() {
        return t("无法识别当前启动磁盘", "Unable to identify current boot disk");
    }

    public String ineligibleBanner(String reason) {
        if ("not_usb_boot".equals(reason)) {
            return t("当前系统不是从 USB 启动，已禁用写入内置硬盘以防止覆盖正在运行的系统盘。",
                    "System is not booted from USB. Internal disk installation is disabled to prevent overwriting the active system disk.");
        }
        if ("no_internal_disk".equals(reason)) {
            return t("未检测到可用的内置非 USB 硬盘。",
                    "No eligible internal non-USB disk was detected.");
        }
        if ("internal_disk_too_small".equals(reason)) {
            return t("内置硬盘容量不足，无法容纳系统与用户数据分区。",
                    "Internal disk is too small for the system and fresh userdata partitions.");
        }
        if ("unsupported_source_layout".equals(reason)) {
            return t("当前启动盘不是标准 5 分区通用布局，请先使用最新 5 分区镜像启动后再安装。",
                    "Current boot disk does not use the standard 5-partition universal layout.");
        }
        if ("source_esp_incomplete".equals(reason)) {
            return t("当前启动 ESP 缺少必要引导文件，无法复制到内置硬盘。",
                    "Current boot ESP is missing required boot files.");
        }
        if ("installer_missing".equals(reason)) {
            return t("当前镜像未包含内置硬盘安装组件。",
                    "This image does not include the internal-disk installer component.");
        }
        return t("当前环境不满足写入内置硬盘条件：" + reason,
                "Current environment is not eligible for internal disk installation: " + reason);
    }

    public String targetSection() {
        return t("目标内置硬盘", "Target internal disk");
    }

    public String targetSummary(String dev, String bus, long bytes, boolean enoughSpace) {
        String busTag = bus == null || bus.isEmpty() ? "INTERNAL" : bus.toUpperCase(Locale.US);
        String cap = DiskInstallPolicy.formatBytes(bytes);
        if (!enoughSpace) {
            return t(dev + " · " + busTag + " · " + cap + "（容量不足）",
                    dev + " · " + busTag + " · " + cap + " (insufficient space)");
        }
        return dev + " · " + busTag + " · " + cap;
    }

    public String planSection() {
        return t("全新安装说明", "Fresh installation plan");
    }

    public String planDetails(long superBytes, long espBytes, long targetBytes) {
        long estData = Math.max(0L, targetBytes - superBytes - espBytes - (70L * 1024L * 1024L));
        return t("• 重建 5 分区 GPT（super " + DiskInstallPolicy.formatBytes(superBytes)
                        + "、metadata 64 MiB、misc 4 MiB、SQ2PROBE " + DiskInstallPolicy.formatBytes(espBytes)
                        + "、全新 userdata 约 " + DiskInstallPolicy.formatBytes(estData) + "）\n"
                        + "• 原样复制当前系统的 super 分区与完整 SHA-256 读回校验\n"
                        + "• 写入标准 UEFI 引导（\\EFI\\BOOT\\BOOTAA64.EFI + GRUB 菜单 + 内核与固件）\n"
                        + "• 不保留原内置硬盘上的任何系统或文件，也不迁移当前 USB 盘的用户数据",
                "• Create fresh 5-partition GPT (super " + DiskInstallPolicy.formatBytes(superBytes)
                        + ", metadata 64 MiB, misc 4 MiB, SQ2PROBE " + DiskInstallPolicy.formatBytes(espBytes)
                        + ", fresh userdata ~" + DiskInstallPolicy.formatBytes(estData) + ")\n"
                        + "• Copy running super partition with full SHA-256 readback verification\n"
                        + "• Install standard UEFI boot (\\EFI\\BOOT\\BOOTAA64.EFI + GRUB + kernel & firmware)\n"
                        + "• Existing internal disk data is erased and USB userdata is NOT migrated");
    }

    public String batteryWarning(int levelPct) {
        return t("当前电量偏低（" + levelPct + "% 且未充电），写入过程需要数分钟，建议接通电源后再操作。",
                "Battery is low (" + levelPct + "% and not charging). Connect AC power before installing.");
    }

    public String dangerTitle() {
        return t("高危操作警告：将清空内置硬盘", "Warning: Internal Disk Will Be Erased");
    }

    public String dangerBody() {
        return t("此操作会彻底抹除所选内置硬盘上的全部分区与数据（包括原有的 Windows 或其他系统），且无法恢复。当前 USB 盘上的应用、账户与文件不会被迁移，内置硬盘将作为全新系统启动。",
                "This permanently erases all partitions and data on the selected internal disk (including Windows or any other OS). Apps, accounts, and files on your USB drive will NOT be copied.");
    }

    public String acknowledgeCheckbox() {
        return t("我已知晓：将彻底清空所选内置硬盘的全部数据，并以全新系统安装",
                "I understand all data on the selected internal disk will be permanently erased for a fresh install");
    }

    public String startInstallButton() {
        return t("清空并写入内置硬盘", "Erase & Install to Internal Disk");
    }

    public String confirmDialogTitle() {
        return t("最后确认：擦除内置硬盘", "Final Confirmation: Erase Internal Disk");
    }

    public String confirmDialogMessage(String model, String dev, long bytes) {
        return t("即将彻底清空以下内置硬盘并全新写入当前系统：\n\n"
                        + "磁盘：" + model + "\n"
                        + "设备：" + dev + "\n"
                        + "容量：" + DiskInstallPolicy.formatBytes(bytes) + "\n\n"
                        + "写入期间请勿断电或拔除当前 USB 启动盘。确认继续吗？",
                "You are about to permanently erase this internal disk and perform a fresh install:\n\n"
                        + "Disk: " + model + "\n"
                        + "Device: " + dev + "\n"
                        + "Size: " + DiskInstallPolicy.formatBytes(bytes) + "\n\n"
                        + "Do NOT power off or unplug the USB boot drive during installation. Continue?");
    }

    public String confirmDialogPositive() {
        return t("确认擦除并安装", "Erase & Install");
    }

    public String installingNotice() {
        return t("正在写入内置硬盘，请保持屏幕常亮并切勿拔除 USB 启动盘或强制关机…",
                "Installing to internal disk. Keep power connected and do NOT unplug the USB boot drive…");
    }

    public String progressLabel(String stage, String detail) {
        String base;
        if ("preparing".equals(stage)) {
            base = t("正在校验磁盘并卸载旧分区…", "Validating disk & unmounting old partitions…");
        } else if ("partitioning".equals(stage)) {
            base = t("正在写入全新 5 分区 GPT 分区表…", "Writing fresh 5-partition GPT…");
        } else if ("copying_super".equals(stage)) {
            base = t("正在写入系统镜像 (super)…", "Copying system image (super)…");
        } else if ("verifying_super".equals(stage)) {
            base = t("正在读回校验系统镜像 SHA-256…", "Verifying written system SHA-256…");
        } else if ("formatting_esp".equals(stage)) {
            base = t("正在格式化 EFI 启动分区 (SQ2PROBE)…", "Formatting EFI boot partition (SQ2PROBE)…");
        } else if ("copying_esp".equals(stage)) {
            base = t("正在复制并校验标准 UEFI 引导载荷…", "Copying & verifying standard UEFI boot payload…");
        } else if ("formatting_data".equals(stage)) {
            base = t("正在格式化全新 metadata / userdata 分区…", "Formatting fresh metadata & userdata…");
        } else if ("finalizing".equals(stage)) {
            base = t("正在落盘同步并复核分区表…", "Syncing disk & verifying final GPT…");
        } else if ("done".equals(stage)) {
            base = t("内置硬盘写入完成", "Internal disk installation complete");
        } else {
            base = t("正在安装系统到内置硬盘…", "Installing to internal disk…");
        }
        return (detail != null && !detail.isEmpty()) ? (base + " (" + detail + ")") : base;
    }

    public String doneTitle() {
        return t("写入内置硬盘已完成", "Installation Complete");
    }

    public String doneBody(DiskInstallPolicy.InstallResult res) {
        String shaShort = res.superSha256.length() >= 16
                ? res.superSha256.substring(0, 12) + "…" + res.superSha256.substring(res.superSha256.length() - 8)
                : res.superSha256;
        return t("当前系统已全新写入 " + res.targetDev + " 并完成 SHA-256 读回校验。\n\n"
                        + "• 全新可用数据分区：" + DiskInstallPolicy.formatBytes(res.userdataBytes) + "\n"
                        + "• 系统镜像校验：" + shaShort + "\n\n"
                        + "下一步操作：\n"
                        + "1. 点击下方“立即关机（推荐）”。\n"
                        + "2. 待设备完全关机后，拔除当前 USB 启动盘。\n"
                        + "3. 按电源键开机，即可从内置硬盘进入全新系统。\n"
                        + "（如未自动进入，请在 Surface UEFI 中保持 Secure Boot 关闭并将 Internal Storage 移至顶部。）",
                "The current system has been freshly installed to " + res.targetDev + " and verified with SHA-256 readback.\n\n"
                        + "• Fresh userdata size: " + DiskInstallPolicy.formatBytes(res.userdataBytes) + "\n"
                        + "• Verified super digest: " + shaShort + "\n\n"
                        + "Next steps:\n"
                        + "1. Tap \"Shut down now (Recommended)\" below.\n"
                        + "2. Once powered off, unplug the USB boot drive.\n"
                        + "3. Press Power to boot cleanly from the internal SSD.\n"
                        + "(Keep Secure Boot disabled and Internal Storage at top of Surface UEFI if needed.)");
    }

    public String powerOffNowButton() {
        return t("立即关机（推荐）", "Shut down now (Recommended)");
    }

    public String powerOffLaterButton() {
        return t("稍后手动关机", "Shut down later");
    }

    public String powerOffDialogTitle() {
        return t("立即关机", "Shut down now");
    }

    public String powerOffDialogMessage() {
        return t("设备将立即关机。完全熄屏关机后请拔除 USB 启动盘，再按电源键从内置硬盘开机。",
                "Device will shut down now. Unplug the USB boot drive after power-off, then press Power to boot from the internal SSD.");
    }

    public String shuttingDownBanner() {
        return t("正在关机… 请在完全关机后拔除 USB 启动盘。",
                "Shutting down… Unplug the USB boot drive once powered off.");
    }

    public String errorMessage(String code) {
        if ("not_usb_boot".equals(code)) {
            return t("当前不是 USB 启动，已中止写盘。", "Aborted: system is not booted from USB.");
        }
        if ("target_disk_busy".equals(code) || "target_swap_busy".equals(code)) {
            return t("目标内置硬盘仍有分区处于占用状态，无法安全卸载。", "Target internal disk is busy and could not be unmounted safely.");
        }
        if ("internal_disk_too_small".equals(code)) {
            return t("目标内置硬盘容量不足。", "Target internal disk is too small.");
        }
        if ("super_copy_verify_failed".equals(code)) {
            return t("写入或读回校验系统镜像 (super) 失败，请检查 USB 盘与内置硬盘连接。",
                    "Failed copying or verifying system image (super). Check USB and internal SSD health.");
        }
        if ("esp_sha256_mismatch".equals(code)) {
            return t("EFI 引导文件复制校验失败。", "EFI boot payload SHA-256 verification failed.");
        }
        if ("partition_nodes_timeout".equals(code)) {
            return t("内核未能在规定时间内枚举新分区节点。", "Timed out waiting for kernel to enumerate new partitions.");
        }
        if ("bridge_timeout".equals(code)) {
            return t("等待安装服务响应超时。", "Timed out waiting for installer daemon.");
        }
        return t("写入内置硬盘失败：" + code, "Internal disk installation failed: " + code);
    }

    public String footer() {
        return t("提示：写入过程仅操作您选中的内置非 USB 硬盘，不会修改当前 USB 启动盘中的数据。",
                "Note: Only the selected internal non-USB disk is modified; your USB boot drive remains untouched.");
    }
}
