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
        return t("从 U 盘启动时全新安装到内置硬盘", "Clean-install running USB system to SSD");
    }

    public String pageSubtitle() {
        return t("将当前 U 盘系统全新写入内置硬盘；会清空内置硬盘且不迁移 U 盘数据。",
                "Clean-install the running USB system to internal disk; erases internal data and does not migrate USB data.");
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
        return t("安装服务未就绪（只读）", "Installer bridge unavailable");
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
            return t("当前非 U 盘启动，已禁用写入内置硬盘。",
                    "Not booted from USB; internal disk install disabled.");
        }
        if ("no_internal_disk".equals(reason)) {
            return t("未检测到可用的内置硬盘。", "No eligible internal disk detected.");
        }
        if ("internal_disk_too_small".equals(reason)) {
            return t("内置硬盘容量不足。", "Internal disk too small.");
        }
        if ("unsupported_source_layout".equals(reason)) {
            return t("当前启动盘不是标准 5 分区布局。", "Boot disk does not use the 5-partition layout.");
        }
        if ("source_esp_incomplete".equals(reason)) {
            return t("启动 ESP 缺少必要引导文件。", "Boot ESP is missing required boot files.");
        }
        if ("installer_missing".equals(reason)) {
            return t("当前镜像未包含内置硬盘安装组件。", "Internal disk installer component missing.");
        }
        return t("暂不支持写入内置硬盘：" + reason,
                "Internal disk installation unavailable: " + reason);
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
        return t("• 新建 5 分区（super " + DiskInstallPolicy.formatBytes(superBytes)
                        + "、ESP " + DiskInstallPolicy.formatBytes(espBytes)
                        + "、userdata 约 " + DiskInstallPolicy.formatBytes(estData) + "）\n"
                        + "• 复制 super 并作 SHA-256 校验，写入 UEFI 引导\n"
                        + "• 会清空原内置硬盘，且不迁移 U 盘数据",
                "• Fresh 5-partition GPT (super " + DiskInstallPolicy.formatBytes(superBytes)
                        + ", ESP " + DiskInstallPolicy.formatBytes(espBytes)
                        + ", userdata ~" + DiskInstallPolicy.formatBytes(estData) + ")\n"
                        + "• Copy & SHA-256 verify super, install UEFI boot\n"
                        + "• Erases internal disk; USB data is not migrated");
    }

    public String batteryWarning(int levelPct) {
        return t("当前电量较低（" + levelPct + "% 未充电），建议接通电源。",
                "Battery low (" + levelPct + "%, not charging). Connect power.");
    }

    public String dangerTitle() {
        return t("高危操作警告：将清空内置硬盘", "Warning: Internal Disk Will Be Erased");
    }

    public String dangerBody() {
        return t("将永久清空所选内置硬盘上的全部分区与系统，且不迁移当前 U 盘数据。",
                "Permanently erases all partitions and data on the selected disk. USB drive data is not migrated.");
    }

    public String acknowledgeCheckbox() {
        return t("我已确认清空所选内置硬盘并全新安装",
                "I confirm erasing the selected internal disk for a fresh install");
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
        return t("仅写入所选内置硬盘，不修改当前 U 盘。",
                "Only modifies the selected internal disk; USB boot drive is untouched.");
    }
}
