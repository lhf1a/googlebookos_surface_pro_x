package org.surface.advancedcontrol;

import java.util.Locale;

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
        return t("图形驱动、性能调度与触摸校准", "Graphics driver, performance & touch");
    }

    public String vulkanTitle() {
        return t("Vulkan 配置", "Vulkan Configuration");
    }

    public String vulkanSubtitle(String defaultLabel, int customCount, boolean available) {
        if (!available) {
            return t("当前镜像缺少双 Vulkan 路由器", "Dual-Vulkan router unavailable in this image");
        }
        if (customCount > 0) {
            return t("默认：" + defaultLabel + " · 已自定义 " + customCount + " 个应用",
                    "Default: " + defaultLabel + " · " + customCount + " custom");
        }
        return t("默认：" + defaultLabel + " · 全部使用默认配置",
                "Default: " + defaultLabel + " · All apps using default");
    }

    public String perfTitle() {
        return t("性能模式", "Performance Mode");
    }

    public String perfSubtitle(String currentGov, boolean available) {
        if (!available || currentGov == null || currentGov.isEmpty()) {
            return t("未检测到可用的 CPU 调度策略", "No CPU frequency governors detected");
        }
        if ("mixed".equals(currentGov)) {
            return t("当前策略：多核心混合状态", "Active policy: Mixed across clusters");
        }
        return t("当前调度策略：" + governorShortName(currentGov),
                "Active policy: " + governorShortName(currentGov));
    }

    public String homeFooter() {
        return t("设置通过内置系统桥接保存并在重启后自动恢复。救援模式（sq2.rescue=1）下会自动回退到安全默认配置。",
                "Settings are saved via the built-in system bridge and restored on reboot. Safe defaults apply automatically in rescue mode (sq2.rescue=1).");
    }

    public String bridgeMissingBanner() {
        return t("系统桥接服务未响应：当前仅可查看状态，修改功能需要配套的 Hybrid 系统镜像。",
                "System bridge is not responding: read-only mode. Saving changes requires a matching Hybrid system image.");
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
            return t("所有未单独配置的应用默认使用 MESA (Turnip) 驱动。重新打开应用后生效。",
                    "All non-customized apps default to MESA (Turnip). Takes effect on next app launch.");
        }
        if ("qcom".equals(defaultDriver)) {
            return t("所有未单独配置的普通应用默认使用 Qcom (Qualcomm QTI) 驱动；系统组件与 SystemUI 保持受保护的 MESA。",
                    "All non-customized regular apps default to Qcom (Qualcomm QTI); System & SystemUI stay protected on MESA.");
        }
        String base = "qcom".equals(policyDefault) ? "Qcom" : "MESA";
        return t("自动策略（出厂默认）：基础默认 " + base + "，Unity/Unreal 游戏自动路由至 Qcom，浏览器与系统组件使用 MESA。",
                "Auto Policy (factory default): base " + base + ", Unity/Unreal games route to Qcom, browsers & system use MESA.");
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
            t("跟随默认配置", "Follow Default"),
            t("MESA (Turnip)", "MESA (Turnip)"),
            t("Qcom (Qualcomm QTI · 实验性)", "Qcom (Qualcomm QTI · Experimental)")
        };
    }

    public String searchHint() {
        return t("搜索应用名称或包名", "Search app name or package");
    }

    public String customSectionHeader(int count) {
        return t("已手动修改（" + count + "）", "Customized (" + count + ")");
    }

    public String defaultSectionHeader(int count) {
        return t("其他应用（" + count + "）", "Other Apps (" + count + ")");
    }

    public String vulkanSummaryBar(int user, int total, int custom, boolean available) {
        if (!available) {
            return t("当前镜像缺少双 Vulkan 路由器或驱动，请先更新 Hybrid 镜像。",
                    "Dual-Vulkan router or drivers missing. Please update your Hybrid image.");
        }
        if (custom > 0) {
            return t("用户 " + user + " · 共 " + total + " 个应用 · " + custom + " 项已修改（已置顶）",
                    "User " + user + " · " + total + " apps · " + custom + " customized (pinned first)");
        }
        return t("用户 " + user + " · 共 " + total + " 个应用 · 点击应用单独配置",
                "User " + user + " · " + total + " apps · Tap an app to configure");
    }

    public String emptySearchResult() {
        return t("没有匹配的应用", "No matching applications");
    }

    public String cancel() {
        return t("取消", "Cancel");
    }

    public String saveOnly() {
        return t("保存", "Save");
    }

    public String saveAndStop() {
        return t("保存并停止应用", "Save & Force Stop");
    }

    public String confirmStopTitle() {
        return t("停止应用？", "Force stop app?");
    }

    public String confirmStopMessage(String pkg) {
        return t("将强制停止 " + pkg + "，未保存的内容可能丢失。之后请手动重新打开应用使配置生效。",
                "This will force-stop " + pkg + ". Unsaved data may be lost. Reopen the app manually afterward.");
    }

    public String confirmStopAction() {
        return t("确认停止", "Force Stop");
    }

    public String savedAppMessage(boolean stopped) {
        return stopped
                ? t("已保存并停止应用，重新打开后生效。", "Saved and force-stopped. Reopen the app to apply.")
                : t("已保存，完全关闭并重新打开应用后生效。", "Saved. Restart the app for changes to take effect.");
    }

    public String savedDefaultMessage(String driver) {
        return t("默认 Vulkan 配置已切换为：" + driverDisplayName(driver),
                "Default Vulkan configuration set to: " + driverDisplayName(driver));
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
            sb.append(t("核心簇：", "Clusters: ")).append(policies.replace(",", " · "));
        }
        if (savedGov != null && !savedGov.isEmpty()) {
            if (sb.length() > 0) {
                sb.append("\n");
            }
            sb.append(t("重启自动恢复：", "Restores on reboot: ")).append(savedGov);
        } else {
            if (sb.length() > 0) {
                sb.append("\n");
            }
            sb.append(t("当前遵循内核启动默认策略", "Following kernel boot default"));
        }
        return sb.toString();
    }

    public String perfNoGovernors() {
        return t("当前设备未暴露 cpufreq 调度策略节点（可能启用了 sq2.cpufreq=0 或救援模式）。",
                "No cpufreq governor nodes exposed (sq2.cpufreq=0 or rescue mode may be active).");
    }

    public String perfRestoreDefault() {
        return t("恢复系统默认策略", "Restore System Default");
    }

    public String perfSavedMessage(String gov) {
        if (gov == null || gov.isEmpty()) {
            return t("已恢复系统默认调度策略。", "Restored system default scheduling policy.");
        }
        return t("已切换调度策略为：" + governorShortName(gov) + "（重启后自动恢复）",
                "Switched governor to: " + governorShortName(gov) + " (persisted across reboots)");
    }

    public String perfFooter() {
        return t("切换调度策略会立即应用到所有 CPU 频率域并持久保存。硬件温控保护始终保持启用。",
                "Changes apply immediately to all CPU frequency policies and persist across reboots. Hardware thermal throttling remains active.");
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
            return t("极致省电 (powersave)", "Power Save (powersave)");
        }
        if ("userspace".equals(gov)) {
            return t("用户态控制 (userspace)", "Userspace (userspace)");
        }
        return gov == null ? "" : gov;
    }

    public String governorDescription(String gov) {
        if ("performance".equals(gov)) {
            return t("锁定 CPU 最高可用频率，响应最快，功耗与发热最高",
                    "Locks CPU to maximum available frequency for lowest latency; highest power & heat");
        }
        if ("schedutil".equals(gov)) {
            return t("系统默认策略，结合内核调度负载实时动态调频，兼顾性能与续航",
                    "System default; scales frequency dynamically using kernel scheduler utilization");
        }
        if ("ondemand".equals(gov)) {
            return t("负载升高时迅速升至高频，空闲时逐步回落",
                    "Rapidly ramps frequency under load and steps down when idle");
        }
        if ("conservative".equals(gov)) {
            return t("平滑渐进地升降频率，优先控制功耗与温度",
                    "Adjusts frequency gradually to favor power efficiency and lower temperature");
        }
        if ("powersave".equals(gov)) {
            return t("锁定 CPU 最低可用频率，最大化续航（性能受限）",
                    "Locks CPU to minimum available frequency for maximum battery life");
        }
        if ("userspace".equals(gov)) {
            return t("允许用户态工具直接设定固定 CPU 频率",
                    "Allows userspace tools to control CPU frequency directly");
        }
        return t("内核支持的 CPU 调频调度策略", "Kernel-supported CPU frequency scaling governor");
    }

    // ---- Touch Sub-page ----

    public String touchTitle() {
        return t("触摸设置", "Touch");
    }

    public String touchHomeSubtitle(boolean available, boolean noiseSaved, boolean noiseEnabled, boolean profileSaved) {
        if (!available) {
            return t("未检测到可用的 IPTS 触摸设备", "No active IPTS touch device detected");
        }
        String patchText = !noiseSaved
                ? t("幽灵补丁：未配置", "Ghost patch: None")
                : (noiseEnabled ? t("幽灵补丁：已启用", "Ghost patch: On") : t("幽灵补丁：已停用", "Ghost patch: Off"));
        String calText = profileSaved ? t("已校准", "Calibrated") : t("未校准", "Uncalibrated");
        return patchText + " · " + calText;
    }

    public String touchUnavailableBanner() {
        return t("未检测到当前 IPTS 触摸控制器节点，仅可从已保存摘要查看或稍后刷新。",
                "No active IPTS touch controller node detected. Refresh once touchscreen service is ready.");
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
            return t("当前设备尚未保存幽灵触控噪声补丁。可通过「重新校准」自动学习，或「从文件应用校准数据」导入 .noise 文件。",
                    "No ghost-touch noise patch is saved yet. Run Recalibrate to learn one or import a .noise file below.");
        }
        DriverPolicy.NoiseSummary ns = DriverPolicy.parseNoiseSummary(noiseInfo);
        String modelDesc = formatNoiseDetail(ns);
        if (noiseEnabled) {
            return t("已激活并实时过滤异常触点：" + modelDesc + "。关闭将立即停用过滤，同时保留已学习数据。",
                    "Actively filtering abnormal contacts: " + modelDesc + ". Turning off pauses filtering while keeping saved data.");
        }
        return t("已暂停过滤（保留已学习数据：" + modelDesc + "）。开启后将立即生效。",
                "Paused (saved data kept: " + modelDesc + "). Toggle on to re-enable immediately.");
    }

    public String touchNoPatchDialogTitle() {
        return t("尚未保存幽灵触控补丁", "No Ghost-Touch Patch Saved");
    }

    public String touchNoPatchDialogMessage() {
        return t("系统尚未为当前触控模组保存 .noise 补丁文件。你可以打开触屏校准向导完成静置/滑动检测，或直接从本地文件导入 .noise 补丁。",
                "No .noise patch file is saved for this touch module yet. Run the touch calibration wizard or import an existing .noise file.");
    }

    public String touchGhostSavedMessage(boolean enabled) {
        return enabled
                ? t("幽灵触控补丁已启用并即时应用到触摸服务。", "Ghost-touch patch enabled and applied live.")
                : t("幽灵触控补丁已停用（已保留原补丁文件）。", "Ghost-touch patch disabled (saved patch kept on disk).");
    }

    public String touchSummarySection() {
        return t("当前触摸校准数据摘要", "Current Calibration Summary");
    }

    public String touchLabelHardware() {
        return t("硬件标识与服务", "Hardware & Service");
    }

    public String touchValueHardware(String hidId, String deviceId, boolean iptsdRunning) {
        String shortId = (deviceId != null && deviceId.length() >= 12) ? deviceId.substring(0, 12) : "—";
        String hid = (hidId != null && !hidId.isEmpty()) ? hidId : "—";
        String svc = iptsdRunning ? t("iptsd 运行中", "iptsd running") : t("iptsd 未运行", "iptsd stopped");
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
        return t("打开触屏校准应用，执行幽灵触控检测与坐标校准", "Open the touch calibration wizard for noise & coordinate calibration");
    }

    public String touchImportTitle() {
        return t("从文件应用校准数据", "Apply Calibration from File");
    }

    public String touchImportSummary() {
        return t("选择并校验导出的校准文件（.spkcal）后立即应用", "Pick and validate an exported calibration file (.spkcal) to apply");
    }

    public String touchExportTitle() {
        return t("导出校准数据", "Export Calibration");
    }

    public String touchExportSummary(boolean hasAnySaved) {
        if (!hasAnySaved) {
            return t("暂无已保存的校准数据可导出", "No saved calibration data to export");
        }
        return t("将当前触摸校准与幽灵触控补丁导出为单个文件", "Export current touch calibration and patch as a single file");
    }

    public String touchClearTitle() {
        return t("清除校准数据", "Clear Calibration Data");
    }

    public String touchClearSummary() {
        return t("清除坐标曲线、幽灵触控补丁与点击策略，恢复出厂默认", "Delete coordinate curves, ghost-touch patch, and tap policy");
    }

    public String touchClearDialogTitle() {
        return t("清除触摸校准数据？", "Clear touch calibration data?");
    }

    public String touchClearDialogMessage() {
        return t("将删除当前设备保存的所有触摸校准文件（坐标曲线、幽灵触控噪声补丁、连点策略），并立即恢复默认未校准状态。需要时可随时重新校准或从文件导入。",
                "This deletes all saved calibration data (.profile, .noise, .tap) for this device and restores uncalibrated defaults immediately.");
    }

    public String touchClearConfirm() {
        return t("确认清除", "Clear Data");
    }

    public String touchClearedMessage() {
        return t("已清除当前设备的触摸校准数据并恢复默认配置。", "Touch calibration data cleared and defaults restored.");
    }

    public String touchImportSuccess(String kind) {
        if ("bundle".equals(kind)) {
            return t("已从校准文件验证并应用当前设备的校准与补丁配置。",
                    "Validated and applied calibration bundle from file.");
        }
        if ("noise".equals(kind)) {
            return t("已从文件验证并应用幽灵触控补丁（.noise）。", "Validated and applied ghost-touch patch (.noise) from file.");
        }
        if ("tap".equals(kind)) {
            return t("已从文件验证并应用点击连续性策略（.tap）。", "Validated and applied tap continuity policy (.tap) from file.");
        }
        return t("已从文件验证并应用触摸坐标校准（.profile）。", "Validated and applied coordinate calibration (.profile) from file.");
    }

    public String touchExportSuccess() {
        return t("已将当前触摸校准数据导出为单个文件。", "Exported current touch calibration data to file.");
    }

    public String touchExportFailed(String detail) {
        return t("导出失败：" + detail, "Export failed: " + detail);
    }

    public String touchImportInvalid(String detail) {
        if ("device_mismatch".equals(detail)) {
            return t("导入失败：该校准文件绑定的硬件/面板标识与当前设备不匹配。",
                    "Import failed: file device identity does not match this hardware.");
        }
        if ("invalid_calibration_file".equals(detail)) {
            return t("导入失败：校准文件格式无效或数值越界。",
                    "Import failed: invalid calibration file format or values.");
        }
        return t("导入失败：" + detail, "Import failed: " + detail);
    }

    public String touchCalibrationAppMissing() {
        return t("未找到触屏校准应用（org.surface.touchcalibration.unified）。",
                "Touch calibration app (org.surface.touchcalibration.unified) is not installed.");
    }

    // ---- System Update (OTA) ----

    public String otaTitle() {
        return t("系统更新", "System update");
    }

    public String otaHomeSubtitle() {
        return t("本地增量描述包与完整镜像包更新，可选保留或清除数据",
                "Local delta manifest & full image updates, keep or wipe data");
    }

    public String otaHeroSubtitle() {
        return t("Googlebook OS · Surface Pro X (Hybrid)", "Googlebook OS · Surface Pro X (Hybrid)");
    }

    public String otaBridgeUnavailable() {
        return t("OTA 更新服务未响应：当前镜像未包含内置 OTA 守护进程（spx-ota-boot）。",
                "OTA service is not responding: this image does not include the built-in spx-ota-boot daemon.");
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
        return t("从本地存储选择升级包", "Choose an update package");
    }

    public String otaSelectHint() {
        return t("支持增量/描述更新包（.tar.gz，仅更新变化文件）以及完整磁盘镜像包（.img / .img.gz，全量更新）。",
                "Supports manifest/delta archives (.tar.gz, updates only changed files) and full disk images (.img / .img.gz).");
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
            return t("将离线全量更新 super 分区（" + fmtBytes(superBytes) + "）与系统启动载荷。",
                    "Will update super partition (" + fmtBytes(superBytes) + ") & boot payload offline.");
        }
        if (!compatible) {
            return t("警告：当前设备的 super 分区基线与该增量包不匹配，请使用完整镜像包进行全量更新。",
                    "Warning: current super baseline does not match this delta package. Use a full image instead.");
        }
        return t("super 分区无变化，仅按清单更新变更的内核/驱动/系统载荷文件。",
                "super partition unchanged; only manifest-listed boot & driver files will be updated.");
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
        return t("数据保留选项", "Data retention");
    }

    public String otaKeepDataTitle() {
        return t("保留用户数据（推荐）", "Keep user data (Recommended)");
    }

    public String otaKeepDataSummary() {
        return t("保留已安装应用、账户、Wi-Fi、触摸校准与高级设置，同时保留双系统引导菜单。",
                "Preserves apps, accounts, Wi-Fi, touch calibration, advanced settings, and dual-boot GRUB menu.");
    }

    public String otaWipeDataTitle() {
        return t("不保留数据（全新安装）", "Clean install (Wipe user data)");
    }

    public String otaWipeDataSummary() {
        return t("更新完成后清除 userdata、metadata 与 misc，所有个人文件、应用与触摸校准将被重置。",
                "Wipes userdata, metadata, and misc during offline install. All personal data & touch calibration will be erased.");
    }

    public String otaWipeWarningBanner() {
        return t("已选择全新安装：重启进入离线更新后将清空全部用户数据与触摸校准，此操作不可撤销。",
                "Clean install selected: all user data and touch calibration will be wiped during offline reboot. This cannot be undone.");
    }

    public String otaInstallButton(boolean willBeOffline, boolean wipeData) {
        if (wipeData) {
            return t("准备全新安装并重启", "Stage clean install");
        }
        if (willBeOffline) {
            return t("准备离线全量更新", "Stage offline update");
        }
        return t("立即应用更新", "Apply update now");
    }

    public String otaWipeConfirmTitle() {
        return t("确认清除全部用户数据？", "Wipe all user data?");
    }

    public String otaWipeConfirmMessage() {
        return t("您已选择“不保留数据（全新安装）”。系统重启进入离线更新阶段时，将更新系统并清空全部用户应用、账户、Wi-Fi、个人文件及触摸校准记录。确定继续吗？",
                "You selected Clean install (Wipe user data). On reboot, the offline updater will install the package and erase all user apps, accounts, Wi-Fi, files, and touch calibration. Continue?");
    }

    public String otaWipeConfirmPositive() {
        return t("确认清除并更新", "Wipe & Update");
    }

    public String otaPendingTitle() {
        return t("离线更新已就绪", "Offline update ready");
    }

    public String otaPendingBody(String version, String kind, boolean applySuper, boolean wipeData) {
        String ver = (version == null || version.isEmpty()) ? t("本地升级包", "Local package") : version;
        String scope = applySuper
                ? t("全量更新（含 super + 启动载荷）", "Full update (super + boot payload)")
                : t("描述包更新（启动与系统载荷）", "Manifest update (boot & system payload)");
        String data = wipeData
                ? t("不保留数据（全新安装）", "Clean install (wipe user data)")
                : t("保留用户数据", "Keep user data");
        return t("待安装：" + ver + "\n模式：" + scope + "\n数据：" + data + "\n重启后将在进入 Android 前自动完成安装，期间请勿断电。",
                "Queued: " + ver + "\nMode: " + scope + "\nData: " + data + "\nInstalls automatically before Android boots. Keep power connected.");
    }

    public String otaAppliedRebootTitle() {
        return t("更新已写入，等待重启", "Update applied — restart required");
    }

    public String otaAppliedRebootBody(String version) {
        String ver = (version == null || version.isEmpty()) ? "" : (" (" + version + ")");
        return t("新的内核、驱动与系统载荷已校验并安全写入" + ver + "，用户数据与引导菜单已完整保留。立即重启即可进入新版本。",
                "New kernel, drivers, and system payload have been verified and written" + ver + ". User data and boot menu are preserved. Restart now to boot into the update.");
    }

    public String otaLastSuccessTitle(String version, boolean wiped) {
        String ver = (version == null || version.isEmpty()) ? "" : (" " + version);
        return wiped
                ? t("已完成全新安装" + ver, "Clean install completed" + ver)
                : t("系统已更新至" + ver, "Updated successfully" + ver);
    }

    public String otaLastErrorTitle() {
        return t("上次更新未完成", "Previous update failed");
    }

    public String otaRebootNowButton() {
        return t("立即重启", "Restart now");
    }

    public String otaRebootInstallButton() {
        return t("立即重启并安装", "Restart & install");
    }

    public String otaCancelPendingButton() {
        return t("取消更新", "Cancel update");
    }

    public String otaDismissButton() {
        return t("知道了", "Dismiss");
    }

    public String otaRebootConfirmTitle() {
        return t("立即重启设备？", "Restart device now?");
    }

    public String otaRebootConfirmMessage(boolean isOffline) {
        return isOffline
                ? t("设备将立即重启并在进入 Android 前自动完成离线系统更新。请保持供电充足，更新完成后将自动再次重启进入桌面。",
                    "Device will restart and automatically apply the offline update before Android boots. Keep power connected.")
                : t("设备将立即重启以启用刚刚安装的系统更新。",
                    "Device will restart immediately to boot into the newly applied update.");
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
            return t("无法识别的升级包格式：请选择本系统构建的 OTA 压缩包（.tar.gz）或完整镜像（.img / .img.gz）。",
                    "Unrecognized package format: choose an OTA archive (.tar.gz) or full disk image (.img / .img.gz).");
        }
        if ("invalid_ota_manifest".equals(code) || "unsupported_manifest_schema".equals(code)) {
            return t("升级包缺少有效的更新描述清单（ota-manifest.kv / ota-files.tsv）。",
                    "Package is missing a valid OTA manifest (ota-manifest.kv / ota-files.tsv).");
        }
        if ("invalid_full_image_gpt".equals(code)) {
            return t("完整镜像 GPT 分区表无效，或缺少 super / SQ2PROBE 分区。",
                    "Invalid full image GPT table or missing super / SQ2PROBE partitions.");
        }
        if ("super_partition_too_small".equals(code)) {
            return t("当前磁盘的 super 分区容量小于该升级包要求，无法直接覆盖。",
                    "Target disk super partition is smaller than required by this package.");
        }
        if ("super_baseline_mismatch".equals(code)) {
            return t("该增量包要求的 super 基线与当前系统不一致，请改用完整镜像包全量更新。",
                    "Delta package requires a different super baseline; please use a full image package.");
        }
        if ("online_apply_failed".equals(code)) {
            return t("写入 ESP 载荷失败，请检查 ESP 剩余空间或重新下载升级包。",
                    "Failed to apply ESP payload; check free ESP space or re-download the package.");
        }
        if ("bridge_timeout".equals(code)) {
            return t("等待 OTA 服务响应超时。", "Timed out waiting for OTA daemon.");
        }
        return t("更新操作失败：" + code, "Update failed: " + code);
    }

    public String otaFooter() {
        return t("更新过程自动保留双系统 GRUB 引导菜单与本机启动标识；选择“保留用户数据”时，触摸校准、Wi-Fi、应用与高级设置均完整保留。",
                "Dual-boot GRUB menu and boot markers are always preserved. Touch calibration, Wi-Fi, apps, and settings remain intact when keeping user data.");
    }
}
