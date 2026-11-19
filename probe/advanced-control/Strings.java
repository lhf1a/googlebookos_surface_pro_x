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
        return t("图形驱动与系统性能调度", "Graphics driver & performance tuning");
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
}
