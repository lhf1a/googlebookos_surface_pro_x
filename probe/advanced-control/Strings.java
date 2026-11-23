package org.surface.advancedcontrol;

import java.util.Locale;
import java.util.Map;

/** System-language localization: Chinese for zh* locales, English default for all others. */
public final class Strings {
    public final boolean zh;

    public Strings(Locale locale) {
        this.zh = isChinese(locale);
    }

    public static boolean isChinese(Locale locale) {
        return locale != null && "zh".equalsIgnoreCase(locale.getLanguage());
    }

    private String t(String zhText, String enText) {
        return zh ? zhText : enText;
    }

    public String appTitle() {
        return t("高级设置", "Advanced Settings");
    }

    public String homeSubtitle() {
        return t("图形、性能、触摸、调试、人脸与系统", "Graphics, performance, touch, debug, face & system");
    }

    public String vulkanTitle() {
        return t("Vulkan 配置", "Vulkan Configuration");
    }

    public String vulkanSubtitle(String defaultLabel, int customCount, boolean available) {
        if (!available) {
            return t("不可用", "Unavailable");
        }
        if (customCount > 0) {
            return t("默认：" + defaultLabel + " · 已修改 " + customCount + " 个应用",
                    "Default: " + defaultLabel + " · " + customCount + " custom");
        }
        return t("默认：" + defaultLabel, "Default: " + defaultLabel);
    }

    public String perfTitle() {
        return t("性能模式", "Performance Mode");
    }

    public String perfSubtitle(String currentGov, boolean available) {
        if (!available || currentGov == null || currentGov.isEmpty()) {
            return t("不可用", "Unavailable");
        }
        if ("mixed".equals(currentGov)) {
            return t("混合调度", "Mixed");
        }
        return governorShortName(currentGov);
    }

    public String bridgeMissingBanner() {
        return t("桥接服务未就绪（只读）", "Bridge unavailable (read-only)");
    }

    public String loadingSummary() {
        return t("正在读取…", "Loading…");
    }

    public String loadingStatus() {
        return t("正在同步系统配置…", "Syncing system configuration…");
    }

    public String savingStatus() {
        return t("正在保存配置…", "Saving configuration…");
    }

    public String refreshDesc() {
        return t("刷新", "Refresh");
    }

    public String backDesc() {
        return t("返回", "Back");
    }

    public String clearDesc() {
        return t("清除", "Clear");
    }

    // ---- Vulkan sub-page ----

    public String defaultConfigSection() {
        return t("默认 Vulkan 配置", "Default Vulkan Configuration");
    }

    public String defaultConfigHint(String defaultDriver, String policyDefault) {
        if ("turnip".equals(defaultDriver)) {
            return t("默认 MESA (Turnip)", "Default: MESA (Turnip)");
        }
        if ("qcom".equals(defaultDriver)) {
            return t("默认 Qcom (QTI)", "Default: Qcom (QTI)");
        }
        String base = "qcom".equals(policyDefault) ? "Qcom" : "MESA";
        return t("自动（基础 " + base + " · 游戏自动切 Qcom）",
                "Auto (base " + base + " · games route to Qcom)");
    }

    public String driverAutoLabel() {
        return t("自动", "Auto");
    }

    public String driverMesaLabel() {
        return "MESA";
    }

    public String driverQcomLabel() {
        return "Qcom";
    }

    public String driverDisplayName(String value) {
        if ("turnip".equals(value)) {
            return "MESA (Turnip)";
        }
        if ("qcom".equals(value)) {
            return "Qcom (QTI)";
        }
        return t("自动策略", "Auto Policy");
    }

    public String appDriverBadge(String value) {
        if ("turnip".equals(value)) {
            return "MESA";
        }
        if ("qcom".equals(value)) {
            return "Qcom";
        }
        if (value != null && !value.isEmpty()) {
            return value;
        }
        return t("默认", "Default");
    }

    public String[] appChoiceLabels() {
        return new String[] {
            t("跟随默认", "Follow Default"),
            t("MESA (Turnip)", "MESA (Turnip)"),
            t("Qcom (QTI)", "Qcom (QTI)")
        };
    }

    public String searchHint() {
        return t("搜索应用", "Search apps");
    }

    public String customSectionHeader(int count) {
        return t("已修改（" + count + "）", "Customized (" + count + ")");
    }

    public String defaultSectionHeader(int count) {
        return t("其他应用（" + count + "）", "Other Apps (" + count + ")");
    }

    public String vulkanSummaryBar(int user, int total, int custom, boolean available) {
        if (!available) {
            return t("双 Vulkan 不可用", "Dual-Vulkan unavailable");
        }
        if (custom > 0) {
            return t("共 " + total + " 个应用 · 已修改 " + custom + " 项",
                    total + " apps · " + custom + " customized");
        }
        return t("共 " + total + " 个应用", total + " apps");
    }

    public String emptySearchResult() {
        return t("无匹配应用", "No matching apps");
    }

    public String cancel() {
        return t("取消", "Cancel");
    }

    public String saveOnly() {
        return t("保存", "Save");
    }

    public String saveAndStop() {
        return t("保存并停止", "Save & Stop");
    }

    public String confirmStopTitle() {
        return t("停止应用？", "Force stop app?");
    }

    public String confirmStopMessage(String pkg) {
        return t("强制停止 " + pkg + "？", "Force stop " + pkg + "?");
    }

    public String confirmStopAction() {
        return t("停止", "Stop");
    }

    public String savedAppMessage(boolean stopped) {
        return stopped
                ? t("已保存并停止应用", "Saved and stopped")
                : t("已保存", "Saved");
    }

    public String savedDefaultMessage(String driver) {
        return t("默认 Vulkan：" + driverDisplayName(driver),
                "Default Vulkan: " + driverDisplayName(driver));
    }

    public String saveFailed(String detail) {
        return t("保存失败：" + detail, "Save failed: " + detail);
    }

    public String readFailed(String detail) {
        return t("读取配置失败：" + detail, "Failed to read settings: " + detail);
    }

    // ---- Performance Mode sub-page ----

    public String perfCurrentSection() {
        return t("当前 CPU 调度状态", "Current CPU Scheduling");
    }

    public String perfPoliciesSection() {
        return t("系统支持的调度策略", "Supported Scheduling Policies");
    }

    public String perfClusterDetail(String policies, String savedGov) {
        StringBuilder sb = new StringBuilder();
        if (policies != null && !policies.isEmpty()) {
            sb.append(policies.replace(",", " · "));
        }
        if (savedGov != null && !savedGov.isEmpty()) {
            if (sb.length() > 0) {
                sb.append("\n");
            }
            sb.append(t("已保存：", "Saved: ")).append(savedGov);
        }
        return sb.toString();
    }

    public String perfNoGovernors() {
        return t("未检测到调度策略", "No cpufreq governors detected");
    }

    public String perfRestoreDefault() {
        return t("恢复默认策略", "Restore Default");
    }

    public String perfSavedMessage(String gov) {
        if (gov == null || gov.isEmpty()) {
            return t("已恢复默认策略", "Restored default policy");
        }
        return t("已切换：" + governorShortName(gov), "Switched to: " + governorShortName(gov));
    }

    public String activeTag() {
        return t("当前", "Active");
    }

    public String governorShortName(String gov) {
        if ("performance".equals(gov)) {
            return t("性能模式 (performance)", "Performance (performance)");
        }
        if ("schedutil".equals(gov)) {
            return t("均衡调度 (schedutil)", "Balanced (schedutil)");
        }
        if ("ondemand".equals(gov)) {
            return t("按需调频 (ondemand)", "On-Demand (ondemand)");
        }
        if ("conservative".equals(gov)) {
            return t("保守省电 (conservative)", "Conservative (conservative)");
        }
        if ("powersave".equals(gov)) {
            return t("省电模式 (powersave)", "Power Save (powersave)");
        }
        if ("userspace".equals(gov)) {
            return t("用户态 (userspace)", "Userspace (userspace)");
        }
        return gov == null ? "" : gov;
    }

    public String governorDescription(String gov) {
        if ("performance".equals(gov)) {
            return t("最高频率", "Maximum frequency");
        }
        if ("schedutil".equals(gov)) {
            return t("负载动态调频", "Scheduler-guided scaling");
        }
        if ("ondemand".equals(gov)) {
            return t("按需升降频", "Dynamic load scaling");
        }
        if ("conservative".equals(gov)) {
            return t("渐进调频", "Gradual scaling");
        }
        if ("powersave".equals(gov)) {
            return t("最低频率", "Minimum frequency");
        }
        if ("userspace".equals(gov)) {
            return t("用户态控频", "Userspace frequency control");
        }
        return "";
    }

    // ---- Touch Sub-page ----

    public String touchTitle() {
        return t("触摸设置", "Touch");
    }

    public String touchHomeSubtitle(boolean available, boolean noiseSaved, boolean noiseEnabled, boolean profileSaved) {
        return touchHomeSubtitle(available, false, noiseSaved, noiseEnabled, profileSaved);
    }

    public String touchHomeSubtitle(boolean available, boolean touchDisabled, boolean noiseSaved,
                                    boolean noiseEnabled, boolean profileSaved) {
        if (touchDisabled) {
            return t("触摸屏已禁用", "Touchscreen disabled");
        }
        if (!available) {
            return t("未检测到触摸设备", "No touch device");
        }
        String patchText = !noiseSaved
                ? t("幽灵补丁：未配置", "Ghost patch: None")
                : (noiseEnabled ? t("幽灵补丁：开", "Ghost patch: On") : t("幽灵补丁：关", "Ghost patch: Off"));
        String calText = profileSaved ? t("已校准", "Calibrated") : t("未校准", "Uncalibrated");
        return patchText + " · " + calText;
    }

    public String touchDisableTitle() {
        return t("禁用触摸", "Disable Touchscreen");
    }

    public String touchDisableBadge(boolean disabled) {
        return disabled ? t("已禁用", "Disabled") : t("正常启用", "Active");
    }

    public String touchDisableDescription(boolean disabled) {
        return disabled
                ? t("已停用触控（重启后保持），键鼠正常",
                    "Touch off (persists after reboot); keyboard & mouse still work")
                : t("停用屏幕触控（适合外接键鼠或触控异常时）",
                    "Turn off touchscreen (for external input or faulty digitizer)");
    }

    public String touchDisableDialogTitle() {
        return t("禁用触摸屏？", "Disable touchscreen?");
    }

    public String touchDisableDialogMessage() {
        return t("禁用后立即停用触控，重启后保持。\n\n请确认已有键盘、鼠标或 ADB 可恢复。",
                 "Touch stops immediately and stays off after reboot.\n\nEnsure a keyboard, mouse, or ADB is available to re-enable.");
    }

    public String touchDisableConfirm() {
        return t("禁用触摸", "Disable");
    }

    public String touchDisableSavedMessage(boolean disabled) {
        return disabled
                ? t("触摸屏已禁用", "Touchscreen disabled")
                : t("触摸屏已重新启用", "Touchscreen re-enabled");
    }

    public String touchUnavailableBanner() {
        return t("未检测到触摸设备", "Touch device unavailable");
    }

    public String touchGhostTitle() {
        return t("幽灵触控补丁", "Ghost-Touch Patch");
    }

    public String touchGhostBadge(boolean noiseSaved, boolean noiseEnabled) {
        if (!noiseSaved) {
            return t("未配置", "None");
        }
        return noiseEnabled ? t("已启用", "Active") : t("已停用", "Disabled");
    }

    public String touchGhostDescription(boolean noiseSaved, boolean noiseEnabled, String noiseInfo) {
        if (!noiseSaved) {
            return t("未保存补丁", "No patch saved");
        }
        DriverPolicy.NoiseSummary ns = DriverPolicy.parseNoiseSummary(noiseInfo);
        String modelDesc = formatNoiseDetail(ns);
        return (noiseEnabled ? t("已启用 · ", "Active · ") : t("已停用 · ", "Disabled · ")) + modelDesc;
    }

    public String touchNoPatchDialogTitle() {
        return t("未保存幽灵触控补丁", "No Ghost-Touch Patch");
    }

    public String touchNoPatchDialogMessage() {
        return t("请先运行校准或导入 .noise 文件。", "Run calibration or import a .noise file first.");
    }

    public String touchGhostSavedMessage(boolean enabled) {
        return enabled
                ? t("幽灵触控补丁已启用", "Ghost-touch patch enabled")
                : t("幽灵触控补丁已停用", "Ghost-touch patch disabled");
    }

    public String touchSummarySection() {
        return t("当前触摸校准数据摘要", "Current Calibration Summary");
    }

    public String touchLabelHardware() {
        return t("硬件标识与服务", "Hardware & Service");
    }

    public String touchValueHardware(String hidId, String deviceId, boolean iptsdRunning) {
        return touchValueHardware(hidId, deviceId, iptsdRunning, false);
    }

    public String touchValueHardware(String hidId, String deviceId, boolean iptsdRunning, boolean touchDisabled) {
        String shortId = (deviceId != null && deviceId.length() >= 12) ? deviceId.substring(0, 12) : "—";
        String hid = (hidId != null && !hidId.isEmpty()) ? hidId : "—";
        String svc = touchDisabled
                ? t("已禁用", "disabled")
                : (iptsdRunning ? t("iptsd 运行中", "iptsd running") : t("iptsd 未运行", "iptsd stopped"));
        return hid + " · ID " + shortId + " · " + svc;
    }

    public String touchLabelCoords() {
        return t("坐标偏移校准", "Coordinate Curves");
    }

    public String touchValueCoords(boolean profileSaved, String calX, String calY, String calTop,
                                   String topStart, String topEnd) {
        if (!profileSaved) {
            return t("未校准（使用出厂直通坐标）", "Uncalibrated (identity pass-through)");
        }
        DriverPolicy.CurveSummary sx = DriverPolicy.summarizeCurve(calX);
        DriverPolicy.CurveSummary sy = DriverPolicy.summarizeCurve(calY);
        DriverPolicy.CurveSummary st = DriverPolicy.summarizeCurve(calTop);
        if (sx.identity && sy.identity && st.identity) {
            return t("已保存直通曲线（无偏移修正）", "Saved identity curves (no offset)");
        }
        return String.format(Locale.ROOT,
                t("已校准 · X %d点(Δ≤%.2f%%) · Y %d点(Δ≤%.2f%%) · 顶部 %d点(Δ≤%.2f%%, %s–%s)",
                  "Calibrated · X %d pts (Δ≤%.2f%%) · Y %d pts (Δ≤%.2f%%) · Top %d pts (Δ≤%.2f%%, %s–%s)"),
                sx.points, sx.maxDeltaPercent,
                sy.points, sy.maxDeltaPercent,
                st.points, st.maxDeltaPercent,
                topStart == null || topStart.isEmpty() ? "0.1" : topStart,
                topEnd == null || topEnd.isEmpty() ? "0.3" : topEnd);
    }

    public String touchLabelThumb() {
        return t("接触面积上限", "Contact Size Limit");
    }

    public String touchValueThumb(String sizeMax) {
        if ("2.4".equals(sizeMax)) {
            return t("2.4 cm（已启用拇指优化）", "2.4 cm (Thumb-optimized)");
        }
        return t("2.0 cm（标准）", "2.0 cm (Standard)");
    }

    public String touchLabelNoise() {
        return t("幽灵噪声模型", "Noise Patch Model");
    }

    public String touchValueNoise(boolean noiseSaved, boolean noiseEnabled, String noiseInfo) {
        if (!noiseSaved) {
            return t("未配置", "Not configured");
        }
        String detail = formatNoiseDetail(DriverPolicy.parseNoiseSummary(noiseInfo));
        String state = noiseEnabled ? t("已启用", "Active") : t("已停用", "Disabled");
        return state + " · " + detail;
    }

    public String touchLabelTap() {
        return t("连点/断触修复", "Tap Continuity Policy");
    }

    public String touchValueTap(boolean tapSaved, String tapLine) {
        if (!tapSaved) {
            return t("未配置", "Not configured");
        }
        DriverPolicy.TapSummary ts = DriverPolicy.parseTapSummary(tapLine);
        if (ts == null) {
            return t("已保存", "Saved");
        }
        return String.format(Locale.ROOT,
                t("SPXTAP1 · 补帧 %d 帧 · %d×%d", "SPXTAP1 · %d repair frame(s) · %d×%d"),
                ts.frames, ts.rows, ts.cols);
    }

    public String formatNoiseDetail(DriverPolicy.NoiseSummary ns) {
        if (ns == null) {
            return t("自定义噪声规则", "Custom noise rule");
        }
        if ("SPXNOISE1".equals(ns.format)) {
            return String.format(Locale.ROOT,
                    t("SPXNOISE1 · %d 个异常格点 (%d×%d)", "SPXNOISE1 · %d spike cell(s) (%d×%d)"),
                    ns.count, ns.rows, ns.cols);
        }
        if ("SPXNOISE2".equals(ns.format)) {
            String axisName = "R".equals(ns.axis) ? t("行", "Row(s)") : t("列", "Col(s)");
            return String.format(Locale.ROOT,
                    t("SPXNOISE2 · %s [%s] (%d×%d)", "SPXNOISE2 · %s [%s] (%d×%d)"),
                    axisName, ns.detail, ns.rows, ns.cols);
        }
        if ("SPXNOISE3".equals(ns.format)) {
            return String.format(Locale.ROOT,
                    t("SPXNOISE3 · %d/%d 电极底噪, 峰值 %d/255 (%d×%d)",
                      "SPXNOISE3 · %d/%d active electrodes, peak %d/255 (%d×%d)"),
                    ns.count, ns.total, ns.maxLevel, ns.rows, ns.cols);
        }
        return ns.format;
    }

    public String touchActionsSection() {
        return t("校准与操作", "Calibration Actions");
    }

    public String touchRecalibrateTitle() {
        return t("重新校准", "Recalibrate");
    }

    public String touchRecalibrateSummary() {
        return t("打开校准向导", "Open calibration wizard");
    }

    public String touchImportTitle() {
        return t("导入校准数据", "Import Calibration");
    }

    public String touchImportSummary() {
        return t("选择 .spkcal / .profile / .noise 文件", "Choose .spkcal / .profile / .noise file");
    }

    public String touchExportTitle() {
        return t("导出校准数据", "Export Calibration");
    }

    public String touchExportSummary(boolean hasAnySaved) {
        if (!hasAnySaved) {
            return t("无数据", "No data");
        }
        return t("导出为 .spkcal 文件", "Export to .spkcal file");
    }

    public String touchClearTitle() {
        return t("清除校准数据", "Clear Calibration");
    }

    public String touchClearSummary() {
        return t("恢复出厂默认", "Restore defaults");
    }

    public String touchClearDialogTitle() {
        return t("清除触摸校准数据？", "Clear calibration data?");
    }

    public String touchClearDialogMessage() {
        return t("将删除已保存的坐标曲线、幽灵补丁与点击策略。", "Deletes saved curves, noise patch, and tap policy.");
    }

    public String touchClearConfirm() {
        return t("清除", "Clear");
    }

    public String touchClearedMessage() {
        return t("已清除校准数据", "Calibration data cleared");
    }

    public String touchImportSuccess(String kind) {
        if ("bundle".equals(kind)) {
            return t("已导入校准包", "Imported calibration bundle");
        }
        if ("noise".equals(kind)) {
            return t("已导入幽灵补丁", "Imported ghost-touch patch");
        }
        if ("tap".equals(kind)) {
            return t("已导入点击策略", "Imported tap policy");
        }
        return t("已导入坐标校准", "Imported coordinate profile");
    }

    public String touchExportSuccess() {
        return t("已导出校准数据", "Calibration exported");
    }

    public String touchExportFailed(String detail) {
        return t("导出失败：" + detail, "Export failed: " + detail);
    }

    public String touchImportInvalid(String detail) {
        if ("device_mismatch".equals(detail)) {
            return t("导入失败：设备不匹配", "Import failed: device mismatch");
        }
        if ("invalid_calibration_file".equals(detail)) {
            return t("导入失败：文件无效", "Import failed: invalid file");
        }
        return t("导入失败：" + detail, "Import failed: " + detail);
    }

    public String touchCalibrationAppMissing() {
        return t("未安装触屏校准应用", "Touch calibration app not installed");
    }

    // ---- Debugging Sub-page ----

    public String debugTitle() {
        return t("调试", "Debugging");
    }

    public String debugHomeSubtitle(boolean adbOn, boolean sshRunning, int keyCount) {
        String adbText = adbOn ? t("网络 ADB：开", "Network ADB: On") : t("网络 ADB：关", "Network ADB: Off");
        String sshText = sshRunning ? t("SSH：开", "SSH: On") : t("SSH：关", "SSH: Off");
        if (keyCount > 0) {
            return adbText + " · " + sshText + " · " + t(keyCount + " 密钥", keyCount + " key(s)");
        }
        return adbText + " · " + sshText;
    }

    public String debugHomeSubtitle(boolean adbOn, boolean sshRunning, int keyCount, boolean vmVpnEnabled) {
        String base = debugHomeSubtitle(adbOn, sshRunning, keyCount);
        return vmVpnEnabled ? base + " · VM VPN" : base;
    }

    public String debugAdbSection() {
        return "ADB";
    }

    public String debugAdbTitle() {
        return t("网络 ADB", "Network ADB");
    }

    public String debugAdbBadge(boolean active) {
        return active ? t("已开启", "On") : t("已关闭", "Off");
    }

    public String debugAdbSummary(boolean enabled, boolean active, String port, String vmIp, String lanIps) {
        String p = (port == null || port.isEmpty() || "-1".equals(port)) ? "5555" : port;
        if (!enabled && !active) {
            return t("端口 " + p + " · 已关闭", "Port " + p + " · Off");
        }
        StringBuilder sb = new StringBuilder();
        sb.append(t("端口 ", "Port ")).append(p);
        if (vmIp != null && !vmIp.isEmpty()) {
            sb.append(" · VM ").append(vmIp).append(":").append(p);
        }
        if (lanIps != null && !lanIps.isEmpty()) {
            sb.append(" · ").append(lanIps.replace(",", " · "));
        }
        return sb.toString();
    }

    public String debugAdbSavedMessage(boolean enabled) {
        return enabled
                ? t("网络 ADB 已开启（5555）", "Network ADB enabled (5555)")
                : t("网络 ADB 已关闭", "Network ADB disabled");
    }

    public String debugLowerUsbSection() {
        return t("下口 USB-C", "Lower USB-C");
    }

    public String debugLowerUsbTitle() {
        return t("下口 USB-C 模式", "Lower USB-C Mode");
    }

    public String debugLowerUsbModeAuto() {
        return t("自动", "Auto");
    }

    public String debugLowerUsbModeAdb() {
        return t("仅 ADB", "ADB Only");
    }

    public String debugLowerUsbModeHost() {
        return t("仅主机", "Host Only");
    }

    public String debugLowerUsbBadge(boolean available, boolean bootLocked, String role, String dpState) {
        if (!available) {
            return t("不可用", "Unavailable");
        }
        if (bootLocked) {
            return t("启动盘锁定", "Boot Locked");
        }
        if ("active".equals(dpState)) {
            return t("DP 输出中", "DP Active");
        }
        if ("pending".equals(dpState)) {
            return t("待切 DP", "DP Pending");
        }
        if ("host".equals(role)) {
            return t("主机模式", "Host");
        }
        if ("device".equals(role)) {
            return t("ADB 就绪", "ADB Ready");
        }
        return t("未知", "Unknown");
    }

    public String debugLowerUsbSummary(boolean available, String mode, String role,
                                       String dpState, boolean bootLocked, boolean watchRunning) {
        if (!available) {
            return t("下口角色控制不可用", "Lower-port role control unavailable");
        }
        if (bootLocked) {
            return t("当前从下口 USB 启动，已锁定为主机模式以保护系统盘。",
                    "Booted from lower USB storage; locked to Host mode.");
        }
        String roleText;
        if ("active".equals(dpState)) {
            roleText = t("DP 显示输出", "DP Display");
        } else if ("host".equals(role)) {
            roleText = t("USB 主机（外设/DP）", "USB Host (Peripherals/DP)");
        } else if ("device".equals(role)) {
            roleText = t("有线 ADB（设备模式）", "Wired ADB (Device)");
        } else {
            roleText = t("未初始化", "Unknown");
        }
        if ("adb".equals(mode)) {
            return t("当前状态：" + roleText + " · 下口固定用于有线 ADB 调试（DP/外设不可用）。",
                    "Active: " + roleText + " · Lower port locked to wired ADB (DP/host disabled).");
        }
        if ("host".equals(mode)) {
            return t("当前状态：" + roleText + " · 下口固定为主机模式（支持 DP 与外设，有线 ADB 关闭）。",
                    "Active: " + roleText + " · Lower port locked to Host (DP/peripherals enabled, USB ADB off).");
        }
        String watchText = watchRunning ? t("监听中", "Watching") : t("未运行", "Stopped");
        return t("当前状态：" + roleText + "（" + watchText + "）· 接入电脑或充电器保持 ADB，接入扩展坞或 DP 显示器自动切为主机模式。",
                "Active: " + roleText + " (" + watchText + ") · Keeps ADB for PCs/chargers; switches to Host automatically for docks and DP displays.");
    }

    public String debugLowerUsbSavedMessage(String mode) {
        if ("adb".equals(mode)) {
            return t("下口模式已设为仅 ADB", "Lower USB-C mode set to ADB Only");
        }
        if ("host".equals(mode)) {
            return t("下口模式已设为仅主机", "Lower USB-C mode set to Host Only");
        }
        return t("下口模式已设为自动切换", "Lower USB-C mode set to Auto");
    }

    public String debugSshSection() {
        return "SSH";
    }

    public String debugSshTitle() {
        return t("SSH 服务", "SSH Service");
    }

    public String debugSshBadge(boolean available, boolean running) {
        if (!available) {
            return t("不可用", "Unavailable");
        }
        return running ? t("运行中", "Running") : t("已停止", "Stopped");
    }

    public String debugSshSummary(boolean available, boolean running, String port,
                                  boolean pwSet, int keyCount, String vmIp, String lanIps) {
        if (!available) {
            return t("不可用", "Unavailable");
        }
        String p = (port == null || port.isEmpty()) ? "22" : port;
        StringBuilder sb = new StringBuilder();
        sb.append("root · ").append(t("端口 ", "Port ")).append(p)
                .append(" · ").append(pwSet ? t("已设密码", "Password set") : t("未设密码", "No password"))
                .append(" · ").append(t(keyCount + " 个密钥", keyCount + " key(s)"));
        if (running) {
            if (vmIp != null && !vmIp.isEmpty()) {
                sb.append("\nVM ").append(vmIp);
            }
            if (lanIps != null && !lanIps.isEmpty()) {
                sb.append(vmIp != null && !vmIp.isEmpty() ? " · " : "\n")
                        .append(lanIps.replace(",", " · "));
            }
        }
        return sb.toString();
    }

    public String debugSshSavedMessage(boolean enabled) {
        return enabled
                ? t("SSH 服务已开启（端口 22）", "SSH service started (port 22)")
                : t("SSH 服务已关闭", "SSH service stopped");
    }

    public String debugSshPasswordTitle() {
        return t("修改密码", "Change Password");
    }

    public String debugSshPasswordSummary(boolean pwSet) {
        return pwSet ? t("root 密码已设置", "root password set") : t("root 密码未设置", "root password not set");
    }

    public String debugSshPasswordDialogTitle() {
        return t("修改 root 密码", "Change root Password");
    }

    public String debugSshPasswordHint() {
        return t("新密码", "New password");
    }

    public String debugSshPasswordConfirmHint() {
        return t("确认新密码", "Confirm password");
    }

    public String debugSshPasswordEmpty() {
        return t("密码不能为空", "Password cannot be empty");
    }

    public String debugSshPasswordMismatch() {
        return t("两次密码不一致", "Passwords do not match");
    }

    public String debugSshPasswordClearButton() {
        return t("清除密码", "Clear");
    }

    public String debugSshPasswordSavedMessage() {
        return t("SSH 密码已更新", "SSH password updated");
    }

    public String debugSshPasswordClearedMessage() {
        return t("SSH 密码已清除", "SSH password cleared");
    }

    public String debugSshKeyImportTitle() {
        return t("导入证书", "Import Key / Certificate");
    }

    public String debugSshKeyImportSummary() {
        return t("OpenSSH 公钥 / PEM 证书", "OpenSSH public key / PEM certificate");
    }

    public String debugSshKeyImportModeTitle() {
        return t("导入方式", "Import Source");
    }

    public String debugSshKeyImportFileOption() {
        return t("从文件选择", "Choose file");
    }

    public String debugSshKeyImportPasteOption() {
        return t("粘贴内容", "Paste text");
    }

    public String debugSshKeyPasteHint() {
        return t("粘贴 OpenSSH 公钥或 PEM 证书", "Paste OpenSSH public key or PEM certificate");
    }

    public String debugSshKeyPasteConfirm() {
        return t("导入", "Import");
    }

    public String debugSshKeyImportedMessage(int added, int total) {
        return t("已导入 " + added + " 项（共 " + total + " 项）",
                "Imported " + added + " (" + total + " total)");
    }

    public String debugSshKeyClearTitle() {
        return t("清除密钥", "Clear Keys");
    }

    public String debugSshKeyClearSummary(int keyCount) {
        return keyCount > 0
                ? t("已保存 " + keyCount + " 个密钥", keyCount + " saved key(s)")
                : t("无已保存密钥", "No saved keys");
    }

    public String debugSshKeyClearDialogTitle() {
        return t("清除全部 SSH 密钥？", "Clear all SSH keys?");
    }

    public String debugSshKeyClearConfirm() {
        return t("清除", "Clear");
    }

    public String debugSshKeyClearedMessage() {
        return t("已清除 SSH 密钥", "SSH keys cleared");
    }

    public String debugVmVpnSection() {
        return "VM";
    }

    public String debugVmVpnTitle() {
        return t("VM 共享 VPN", "VM VPN Sharing");
    }

    public String debugVmVpnBadge(boolean available, boolean enabled, boolean active) {
        if (!available) {
            return t("不可用", "Unavailable");
        }
        if (!enabled) {
            return t("已关闭", "Off");
        }
        return active ? t("已生效", "Active") : t("已开启", "On");
    }

    public String debugVmVpnSavedMessage(boolean enabled) {
        return enabled
                ? t("VM 共享 VPN 已开启", "VM VPN sharing enabled")
                : t("VM 共享 VPN 已关闭", "VM VPN sharing disabled");
    }

    public String debugErrorMessage(String code) {
        if ("invalid_ssh_key".equals(code)) {
            return t("无效的公钥或证书格式", "Invalid key or certificate format");
        }
        if ("invalid_password".equals(code)) {
            return t("密码格式无效", "Invalid password");
        }
        if ("adb_tcp_failed".equals(code)) {
            return t("切换网络 ADB 失败", "Failed to toggle Network ADB");
        }
        if ("ssh_start_failed".equals(code)) {
            return t("启动 SSH 服务失败", "Failed to start SSH service");
        }
        if ("avf_vpn_start_failed".equals(code)) {
            return t("启动 VM VPN 共享失败", "Failed to start VM VPN sharing");
        }
        if ("lower_usb_busy".equals(code)) {
            return t("请先断开下口外设或显示器后再切换",
                    "Disconnect lower-port peripherals or display before switching");
        }
        if ("lower_usb_boot_locked".equals(code)) {
            return t("下口正在承载启动盘，无法切至 ADB",
                    "Lower port carries the boot disk; cannot switch to ADB");
        }
        if ("lower_usb_unavailable".equals(code)) {
            return t("下口模式控制接口不可用", "Lower USB-C control unavailable");
        }
        if ("lower_usb_failed".equals(code)) {
            return t("切换下口 USB-C 模式失败", "Failed to switch Lower USB-C mode");
        }
        if ("mkpasswd_failed".equals(code)) {
            return t("密码加密失败", "Password hashing failed");
        }
        if ("bridge_timeout".equals(code)) {
            return t("桥接服务响应超时", "Bridge timed out");
        }
        return t("操作失败：" + code, "Failed: " + code);
    }

    // ---- System Update (OTA) ----

    public String otaTitle() {
        return t("系统更新", "System update");
    }

    public String otaHomeSubtitle() {
        return t("本地升级包更新", "Local package updates");
    }

    public String otaHeroSubtitle() {
        return "Googlebook OS · Surface Pro X";
    }

    public String otaBridgeUnavailable() {
        return t("OTA 服务未就绪", "OTA service unavailable");
    }

    public String otaVersionLabel(String version) {
        String v = (version == null || version.isEmpty()) ? t("未知版本", "Unknown") : version;
        return t("当前版本：" + v, "Current version: " + v);
    }

    public String otaKernelLabel(String kernel) {
        String k = (kernel == null || kernel.isEmpty()) ? "-" : kernel;
        return t("内核：" + k, "Kernel: " + k);
    }

    public String otaStorageLabel(long espFreeMb, long espTotalMb, long dataFreeMb) {
        return t("ESP 可用 " + espFreeMb + " / " + espTotalMb + " MiB · 数据分区可用 " + dataFreeMb + " MiB",
                "ESP free " + espFreeMb + " / " + espTotalMb + " MiB · Data free " + dataFreeMb + " MiB");
    }

    public String otaSelectSection() {
        return t("本地升级包", "Local update package");
    }

    public String otaSelectTitle() {
        return t("选择本地升级包", "Choose update package");
    }

    public String otaSelectHint() {
        return t("支持 .tar.gz / .img / .img.gz", "Supports .tar.gz / .img / .img.gz");
    }

    public String otaSelectButton() {
        return t("选择升级包", "Select package");
    }

    public String otaChangePackageButton() {
        return t("重新选择", "Choose another");
    }

    public String otaClearSelectionButton() {
        return t("清除选择", "Clear");
    }

    public String otaInspectSection() {
        return t("升级包详情", "Package details");
    }

    public String otaPackageBadge(String kind, boolean superIncluded, String espMode) {
        if ("manifest".equals(kind)) {
            if (superIncluded) {
                return t("完整描述包（含 super）", "Full manifest (includes super)");
            }
            if ("delta".equals(espMode)) {
                return t("增量描述包（仅变更文件）", "Delta manifest (changed files only)");
            }
            return t("轻量描述包（super 无变化）", "Manifest update (super unchanged)");
        }
        if ("full-img-gz".equals(kind)) {
            return t("完整镜像包（.img.gz 全量更新）", "Full disk image (.img.gz)");
        }
        if ("full-img".equals(kind)) {
            return t("完整镜像包（.img 全量更新）", "Full disk image (.img)");
        }
        return t("未知包格式", "Unknown format");
    }

    private static String fmtBytes(long bytes) {
        if (bytes < 0) return "0 B";
        if (bytes < 1024L) return bytes + " B";
        double kb = bytes / 1024.0;
        if (kb < 1024.0) return String.format(Locale.US, "%.1f KiB", kb);
        double mb = kb / 1024.0;
        if (mb < 1024.0) return String.format(Locale.US, "%.1f MiB", mb);
        return String.format(Locale.US, "%.2f GiB", mb / 1024.0);
    }

    private static String fmtShortSha(String sha) {
        if (sha == null || sha.length() < 16) return sha == null ? "" : sha;
        return sha.substring(0, 12) + "…" + sha.substring(sha.length() - 8);
    }

    public String otaSuperSummary(boolean superIncluded, boolean compatible, long superBytes) {
        if (superIncluded) {
            return t("包含 super 分区（" + fmtBytes(superBytes) + "）",
                    "Includes super (" + fmtBytes(superBytes) + ")");
        }
        if (!compatible) {
            return t("super 基线不匹配，请使用完整镜像包",
                    "super baseline mismatch; use a full image");
        }
        return t("仅更新引导与系统载荷", "Updates boot & system payload only");
    }

    public String otaPackageMeta(String version, int fileCount, long pkgBytes, String sha) {
        StringBuilder sb = new StringBuilder();
        if (version != null && !version.isEmpty() && !"full-image".equals(version)) {
            sb.append(t("目标版本：", "Target: ")).append(version).append(" · ");
        }
        if (fileCount > 0) {
            sb.append(t("更新项 ", "Entries: ")).append(fileCount).append(" · ");
        }
        sb.append(fmtBytes(pkgBytes));
        if (sha != null && !sha.isEmpty()) {
            sb.append("\nSHA-256: ").append(fmtShortSha(sha));
        }
        return sb.toString();
    }

    public String otaDataModeSection() {
        return t("数据保留", "Data retention");
    }

    public String otaKeepDataTitle() {
        return t("保留用户数据（推荐）", "Keep user data (Recommended)");
    }

    public String otaKeepDataSummary() {
        return t("保留应用、账户与设置", "Keep apps, accounts & settings");
    }

    public String otaWipeDataTitle() {
        return t("不保留数据（全新安装）", "Clean install (Wipe user data)");
    }

    public String otaWipeDataSummary() {
        return t("清除 userdata、metadata 与 misc", "Wipe userdata, metadata & misc");
    }

    public String otaWipeWarningBanner() {
        return t("全新安装将清空全部用户数据", "Clean install will wipe all user data");
    }

    public String otaInstallButton(boolean willBeOffline, boolean wipeData) {
        if (wipeData) {
            return t("准备全新安装并重启", "Stage clean install");
        }
        if (willBeOffline) {
            return t("准备离线更新", "Stage offline update");
        }
        return t("应用更新", "Apply update");
    }

    public String otaWipeConfirmTitle() {
        return t("确认全新安装？", "Wipe all user data?");
    }

    public String otaWipeConfirmMessage() {
        return t("重启更新时将清空全部用户数据。", "All user data will be erased during update.");
    }

    public String otaWipeConfirmPositive() {
        return t("确认更新", "Wipe & Update");
    }

    public String otaPendingTitle() {
        return t("离线更新已就绪", "Offline update ready");
    }

    public String otaPendingBody(String version, String kind, boolean applySuper, boolean wipeData) {
        String ver = (version == null || version.isEmpty()) ? t("本地升级包", "Local package") : version;
        String scope = applySuper
                ? t("全量更新", "Full update")
                : t("增量更新", "Manifest update");
        String data = wipeData
                ? t("全新安装", "Clean install")
                : t("保留数据", "Keep data");
        return ver + " · " + scope + " · " + data;
    }

    public String otaAppliedRebootTitle() {
        return t("更新已写入，请重启", "Update ready — restart required");
    }

    public String otaAppliedRebootBody(String version) {
        String ver = (version == null || version.isEmpty()) ? "" : (" (" + version + ")");
        return t("已写入" + ver + "，重启生效。", "Written" + ver + ". Restart to apply.");
    }

    public String otaLastSuccessTitle(String version, boolean wiped) {
        String ver = (version == null || version.isEmpty()) ? "" : (" " + version);
        return wiped
                ? t("已完成全新安装" + ver, "Clean install completed" + ver)
                : t("系统已更新至" + ver, "Updated to" + ver);
    }

    public String otaLastErrorTitle() {
        return t("上次更新未完成", "Previous update failed");
    }

    public String otaRebootNowButton() {
        return t("立即重启", "Restart now");
    }

    public String otaRebootInstallButton() {
        return t("重启并安装", "Restart & install");
    }

    public String otaCancelPendingButton() {
        return t("取消更新", "Cancel update");
    }

    public String otaDismissButton() {
        return t("关闭", "Dismiss");
    }

    public String otaRebootConfirmTitle() {
        return t("立即重启？", "Restart now?");
    }

    public String otaRebootConfirmMessage(boolean isOffline) {
        return isOffline
                ? t("重启后自动安装更新。", "Restarting to install update.")
                : t("重启以启用新版本。", "Restarting to boot update.");
    }

    public String otaProgressLabel(String stage, String detail) {
        String base;
        if ("copying".equals(stage)) {
            base = t("正在读取升级包到暂存区…", "Staging update package…");
        } else if ("hashing".equals(stage)) {
            base = t("正在计算并校验 SHA-256…", "Computing SHA-256 digest…");
        } else if ("inspecting".equals(stage)) {
            base = t("正在解析升级包描述与分区表…", "Inspecting manifest & partition table…");
        } else if ("extracting".equals(stage)) {
            base = t("正在解压更新文件…", "Extracting update payload…");
        } else if ("verifying".equals(stage)) {
            base = t("正在逐文件校验完整性…", "Verifying file checksums…");
        } else if ("applying".equals(stage)) {
            base = t("正在安全写入系统载荷…", "Writing verified boot payload…");
        } else if ("staging_offline".equals(stage)) {
            base = t("正在写入离线更新计划…", "Scheduling offline boot update…");
        } else {
            base = t("正在处理升级任务…", "Processing update…");
        }
        return (detail != null && !detail.isEmpty()) ? (base + " (" + detail + ")") : base;
    }

    public String otaErrorMessage(String code) {
        if ("unsupported_package_format".equals(code)) {
            return t("不支持的升级包格式", "Unsupported package format");
        }
        if ("invalid_ota_manifest".equals(code) || "unsupported_manifest_schema".equals(code)) {
            return t("无效的 OTA 清单", "Invalid OTA manifest");
        }
        if ("invalid_full_image_gpt".equals(code)) {
            return t("无效的镜像分区表", "Invalid GPT image");
        }
        if ("super_partition_too_small".equals(code)) {
            return t("super 分区空间不足", "super partition too small");
        }
        if ("super_baseline_mismatch".equals(code)) {
            return t("super 基线不匹配", "super baseline mismatch");
        }
        if ("online_apply_failed".equals(code)) {
            return t("写入 ESP 失败", "Failed to apply ESP payload");
        }
        if ("bridge_timeout".equals(code)) {
            return t("OTA 服务超时", "OTA service timeout");
        }
        return t("更新失败：" + code, "Update failed: " + code);
    }

    // ---- Hall Lid Switch & OV7251 IR Face Unlock ----

    public String faceLidTitle() {
        return t("霍尔与红外解锁", "Lid & IR Face Unlock");
    }

    public String faceLidHomeSubtitle(boolean lidAvailable, boolean lidEnabled,
                                        boolean faceAvailable, boolean faceEnabled) {
        String lidPart = !lidAvailable
                ? t("霍尔不可用", "No Hall lid")
                : (lidEnabled
                        ? t("开盖唤醒开", "Hall lid on")
                        : t("开盖唤醒关", "Hall lid off"));
        String facePart = !faceAvailable
                ? t("红外不可用", "No IR camera")
                : (faceEnabled
                        ? t("人脸解锁开", "IR face on")
                        : t("人脸解锁关", "IR face off"));
        return lidPart + " · " + facePart;
    }

    public String faceLidSection() {
        return t("霍尔开关", "Hall Lid");
    }

    public String faceLidToggleTitle() {
        return t("合盖休眠与开盖唤醒", "Lid sleep & wake");
    }

    public String faceLidBadge(boolean lidAvailable, boolean lidEnabled) {
        if (!lidAvailable) {
            return t("不可用", "Unavailable");
        }
        return lidEnabled ? t("已启用", "Enabled") : t("已禁用", "Disabled");
    }

    public String faceLidSummary(boolean lidAvailable, boolean lidEnabled) {
        if (!lidAvailable) {
            return t("未检测到霍尔传感器", "Hall sensor unavailable");
        }
        if (lidEnabled) {
            return t("合盖休眠、开盖唤醒，反折键盘时自动屏蔽",
                    "Sleep on close, wake on open; ignored when folded back");
        }
        return t("已关闭合盖检测（可避免磁吸皮套误触发）",
                "Lid detection off (avoids magnetic case false triggers)");
    }

    public String faceLidStateFieldLabel() {
        return t("霍尔状态", "Lid state");
    }

    public String faceLidStateLabel(String lidState) {
        if ("OPEN".equals(lidState)) {
            return t("已开盖", "Open");
        }
        if ("CLOSED".equals(lidState)) {
            return t("已合盖", "Closed");
        }
        if ("FOLDED_IGNORED".equals(lidState)) {
            return t("已开盖（键盘反折屏蔽）", "Open (folded back)");
        }
        if ("DISABLED".equals(lidState)) {
            return t("已关闭", "Disabled");
        }
        return t("不可用", "Unavailable");
    }

    public String ssamCoverPostureFieldLabel() {
        return t("键盘盖状态", "Type Cover");
    }

    public String ssamCoverStateLabel(String coverState) {
        if ("folded-back".equals(coverState) || "folded-canvas".equals(coverState)
                || "book".equals(coverState)) {
            return t("反折", "Folded back");
        }
        if ("closed".equals(coverState)) {
            return t("已合盖", "Closed");
        }
        if ("laptop".equals(coverState)) {
            return t("正常打开", "Open (laptop)");
        }
        if ("disconnected".equals(coverState)) {
            return t("未连接", "Disconnected");
        }
        if (coverState == null || coverState.isEmpty() || "unavailable".equals(coverState)) {
            return t("未检测到", "Unavailable");
        }
        return coverState;
    }

    public String faceUnlockSection() {
        return t("红外人脸解锁", "IR Face Unlock");
    }

    public String faceUnlockTitle() {
        return t("红外人脸解锁", "IR Face Unlock");
    }

    public String faceUnlockBadge(boolean faceAvailable, boolean faceEnabled) {
        if (!faceAvailable) {
            return t("不可用", "Unavailable");
        }
        return faceEnabled ? t("已开启", "On") : t("已关闭", "Off");
    }

    public String faceUnlockSummary(boolean faceAvailable, boolean faceEnabled) {
        if (!faceAvailable) {
            return t("未检测到红外相机", "IR camera unavailable");
        }
        if (faceEnabled) {
            return t("认证时按需开启，支持四向识别，数据本地加密",
                    "On-demand only, 4-way orientation, encrypted locally");
        }
        return t("已停用，红外相机不会响应认证", "Disabled; IR camera will not activate");
    }

    public String faceSystemSettingsTitle() {
        return t("系统人脸设置", "System Face Settings");
    }

    public String faceSystemSettingsSummary() {
        return t("录入、管理或删除人脸数据", "Enroll, manage, or delete face data");
    }

    public String faceSystemSettingsUnavailable() {
        return t("无法打开系统人脸设置", "System Face Settings unavailable");
    }

    public String faceHalStatusLabel() {
        return t("人脸服务", "Face service");
    }

    public String faceHalStatusValue(boolean running, boolean avail) {
        if (!avail) {
            return t("不可用", "Unavailable");
        }
        return running ? t("运行中", "Running") : t("待机中", "Idle");
    }

    public String faceSelfCheckTitle() {
        return t("一键自检", "Run Self-Check");
    }

    public String faceSelfCheckSummary() {
        return t("检测红外相机、霍尔开关与识别引擎", "Check IR camera, Hall lid & face engine");
    }

    public String faceSelfCheckResultMessage(String summary) {
        Map<String, String> m = DriverPolicy.parseSelfCheckSummary(summary);
        String st = m.getOrDefault("self_test", "unknown");
        String lid = m.getOrDefault("lid_state", "UNAVAILABLE");
        String bench = m.getOrDefault("ir_bench", "skip");
        String p50 = m.getOrDefault("proc_p50_ms", "na");
        if ("pass".equals(bench)) {
            return t("自检通过 · 延迟 " + p50 + " ms · 霍尔 " + lid,
                    "Self-check passed · " + p50 + " ms · Lid " + lid);
        }
        return t("自检完成（" + st + " / " + bench + "）· 霍尔 " + lid,
                "Self-check done (" + st + " / " + bench + ") · Lid " + lid);
    }

    public String faceHallSavedMessage(boolean enabled) {
        return enabled
                ? t("已开启合盖休眠与开盖唤醒", "Lid sleep & wake enabled")
                : t("已关闭合盖检测", "Lid detection disabled");
    }

    public String faceUnlockSavedMessage(boolean enabled) {
        return enabled
                ? t("已开启红外人脸解锁", "IR Face Unlock enabled")
                : t("已关闭红外人脸解锁", "IR Face Unlock disabled");
    }

    public String faceErrorMessage(String code) {
        if ("hall_lid_cmdline_disabled".equals(code)) {
            return t("启动参数已禁用霍尔开关", "Hall lid disabled by kernel cmdline");
        }
        if ("hall_lid_unavailable".equals(code)) {
            return t("霍尔开关不可用", "Hall lid unavailable");
        }
        if ("invalid_rotation".equals(code)) {
            return t("无效旋转角度", "Invalid rotation value");
        }
        if ("face_cli_missing".equals(code)) {
            return t("缺少自检工具", "spx-face-cli not installed");
        }
        if ("face_self_test_failed".equals(code)) {
            return t("自检失败，请查看系统日志", "Face self-test failed");
        }
        if ("bridge_timeout".equals(code)) {
            return t("桥接服务响应超时", "Bridge timed out");
        }
        return t("操作失败：" + code, "Operation failed: " + code);
    }
}
