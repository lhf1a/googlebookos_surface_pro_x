package org.surface.advancedcontrol;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** Pure policy, sorting, and bridge request encoding, shared by the UI and host tests. */
public final class DriverPolicy {
    public static final String APP_PREFIX = "persist.sq2.vulkan.app.";
    public static final String DEFAULT_DRIVER_PROP = "persist.sq2.vulkan.driver";

    private DriverPolicy() {}

    public static String propertyKey(int uid, String pkg) {
        int appId = uid % 100000;
        if (uid < 0 || appId < 10000 || appId >= 20000 || pkg == null
                || pkg.length() >= 192
                || !pkg.matches("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+")) {
            throw new IllegalArgumentException("Unsupported application identity");
        }
        return APP_PREFIX + (uid / 100000) + "." + pkg;
    }

    public static String value(String value) {
        if ("".equals(value) || "turnip".equals(value) || "qcom".equals(value)) {
            return value;
        }
        throw new IllegalArgumentException("Unknown Vulkan driver: " + value);
    }

    public static String defaultValue(String value) {
        return value(value);
    }

    public static boolean isCustomized(String savedValue) {
        return "turnip".equals(savedValue) || "qcom".equals(savedValue);
    }

    public static String normalizeNonce(String nonce) {
        if (nonce == null || nonce.isEmpty() || nonce.length() > 64
                || !nonce.matches("[a-z0-9-]+")) {
            throw new IllegalArgumentException("Invalid bridge nonce");
        }
        return nonce;
    }

    public static String normalizeGovernor(String governor) {
        if (governor == null || governor.isEmpty()) {
            return "";
        }
        if (governor.length() > 32 || !governor.matches("[A-Za-z0-9_-]+")) {
            throw new IllegalArgumentException("Invalid CPU governor: " + governor);
        }
        return governor;
    }

    public static List<String> parseAvailableGovernors(String raw) {
        List<String> result = new ArrayList<>();
        if (raw == null || raw.trim().isEmpty()) {
            return result;
        }
        Set<String> seen = new LinkedHashSet<>();
        for (String token : raw.trim().split("\\s+")) {
            if (!token.isEmpty() && token.length() <= 32 && token.matches("[A-Za-z0-9_-]+")) {
                if (seen.add(token)) {
                    result.add(token);
                }
            }
        }
        return result;
    }

    /** Manually customized apps always sort before unmodified apps, then by label and package. */
    public static int compareAppRows(boolean customA, String labelA, String pkgA,
                                     boolean customB, String labelB, String pkgB) {
        if (customA != customB) {
            return customA ? -1 : 1;
        }
        String la = labelA == null ? "" : labelA;
        String lb = labelB == null ? "" : labelB;
        int byLabel = la.compareToIgnoreCase(lb);
        if (byLabel != 0) {
            return byLabel;
        }
        String pa = pkgA == null ? "" : pkgA;
        String pb = pkgB == null ? "" : pkgB;
        return pa.compareTo(pb);
    }

    public static String refreshRequest(String nonce) {
        return "refresh\n" + normalizeNonce(nonce) + "\n";
    }

    public static String appVulkanRequest(String nonce, int uid, String pkg, String driver, boolean stop) {
        propertyKey(uid, pkg);
        String n = normalizeNonce(nonce);
        String v = value(driver);
        int userId = uid / 100000;
        return "set-app-vulkan\n" + n + "\n" + userId + "\n" + pkg + "\n" + v + "\n" + (stop ? "1" : "0") + "\n";
    }

    public static String defaultVulkanRequest(String nonce, String driver) {
        return "set-default-vulkan\n" + normalizeNonce(nonce) + "\n" + defaultValue(driver) + "\n";
    }

    public static String cpuGovernorRequest(String nonce, String governor) {
        return "set-cpu-governor\n" + normalizeNonce(nonce) + "\n" + normalizeGovernor(governor) + "\n";
    }

    public static String saveCommand(int uid, String pkg, String driver, boolean stop) {
        String key = propertyKey(uid, pkg);
        String v = value(driver);
        // Only validated identifiers reach the shell; one property is one atomic update.
        return "set -e; test \"$(/system/bin/id -u)\" = 0; /system/bin/setprop '" + key
                + "' '" + v + "'; test \"$(/system/bin/getprop '" + key + "')\" = '" + v + "'"
                + (stop ? "; /system/bin/am force-stop --user " + (uid / 100000) + " '" + pkg + "'" : "");
    }

    public static boolean matchesQuery(String label, String pkg, String query) {
        if (query == null || query.isEmpty()) {
            return true;
        }
        String q = query.toLowerCase(Locale.ROOT);
        String combined = ((label == null ? "" : label) + " " + (pkg == null ? "" : pkg)).toLowerCase(Locale.ROOT);
        return combined.contains(q);
    }
}
