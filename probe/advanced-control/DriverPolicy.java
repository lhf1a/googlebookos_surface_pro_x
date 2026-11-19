package org.surface.advancedcontrol;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** Pure policy, sorting, touch summary, and bridge request encoding, shared by the UI and host tests. */
public final class DriverPolicy {
    public static final String APP_PREFIX = "persist.sq2.vulkan.app.";
    public static final String DEFAULT_DRIVER_PROP = "persist.sq2.vulkan.driver";
    public static final int MAX_TOUCH_IMPORT_BYTES = 132096;

    public static final class CurveSummary {
        public final int points;
        public final boolean identity;
        public final double maxDeltaPercent;

        public CurveSummary(int points, boolean identity, double maxDeltaPercent) {
            this.points = points;
            this.identity = identity;
            this.maxDeltaPercent = maxDeltaPercent;
        }
    }

    public static final class NoiseSummary {
        public final String format;
        public final int rows;
        public final int cols;
        public final int count;
        public final int total;
        public final int maxLevel;
        public final String axis;
        public final String detail;

        public NoiseSummary(String format, int rows, int cols, int count, int total,
                            int maxLevel, String axis, String detail) {
            this.format = format;
            this.rows = rows;
            this.cols = cols;
            this.count = count;
            this.total = total;
            this.maxLevel = maxLevel;
            this.axis = axis;
            this.detail = detail;
        }
    }

    public static final class TapSummary {
        public final int rows;
        public final int cols;
        public final int frames;

        public TapSummary(int rows, int cols, int frames) {
            this.rows = rows;
            this.cols = cols;
            this.frames = frames;
        }
    }

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

    public static String touchGhostPatchRequest(String nonce, boolean enabled) {
        return "set-touch-ghost-patch\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String clearTouchCalibrationRequest(String nonce) {
        return "clear-touch-calibration\n" + normalizeNonce(nonce) + "\n";
    }

    public static String exportTouchCalibrationRequest(String nonce) {
        return "export-touch-calibration\n" + normalizeNonce(nonce) + "\n";
    }

    public static String defaultTouchExportFilename(String deviceId) {
        if (deviceId != null && deviceId.matches("[0-9a-f]{8,64}")) {
            return "surface-touch-" + deviceId.substring(0, 8) + ".spkcal";
        }
        return "surface-touch.spkcal";
    }

    public static String detectTouchImportKind(String rawContent) {
        if (rawContent == null) {
            throw new IllegalArgumentException("Empty calibration file");
        }
        if (rawContent.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("Invalid binary calibration file");
        }
        String normalized = rawContent.replace("\r\n", "\n").replace('\r', '\n');
        byte[] bytes = normalized.getBytes(StandardCharsets.UTF_8);
        if (bytes.length == 0 || bytes.length > MAX_TOUCH_IMPORT_BYTES) {
            throw new IllegalArgumentException("Calibration file is empty or too large");
        }
        String firstNonEmpty = "";
        for (String line : normalized.split("\n")) {
            String trimmed = line.trim();
            if (!trimmed.isEmpty()) {
                firstNonEmpty = trimmed;
                break;
            }
        }
        if (firstNonEmpty.isEmpty()) {
            throw new IllegalArgumentException("Empty calibration file");
        }
        if ("SPXTOUCH1".equals(firstNonEmpty)
                && (normalized.contains("\n[Profile]\n")
                        || normalized.contains("\n[Noise]\n")
                        || normalized.contains("\n[Tap]\n"))) {
            return "bundle";
        }
        if (firstNonEmpty.startsWith("SPXNOISE1 ") || firstNonEmpty.startsWith("SPXNOISE2 ")
                || firstNonEmpty.startsWith("SPXNOISE3 ")) {
            return "noise";
        }
        if (firstNonEmpty.startsWith("SPXTAP1 ")) {
            return "tap";
        }
        if (normalized.contains("Version=1") && normalized.contains("CalibrationX=")) {
            return "profile";
        }
        throw new IllegalArgumentException("Unrecognized calibration file format");
    }

    public static String importTouchCalibrationRequest(String nonce, String rawContent) {
        detectTouchImportKind(rawContent);
        String normalized = rawContent.replace("\r\n", "\n").replace('\r', '\n');
        if (!normalized.endsWith("\n")) {
            normalized += "\n";
        }
        return "import-touch-calibration\n" + normalizeNonce(nonce) + "\n" + normalized;
    }

    public static CurveSummary summarizeCurve(String curveText) {
        if (curveText == null || curveText.trim().isEmpty()) {
            return new CurveSummary(0, true, 0.0);
        }
        String[] tokens = curveText.trim().split("\\s+");
        int count = 0;
        double maxDelta = 0.0;
        boolean allEqual = true;
        for (String tok : tokens) {
            String[] parts = tok.split(":");
            if (parts.length != 2) {
                continue;
            }
            try {
                double from = Double.parseDouble(parts[0]);
                double to = Double.parseDouble(parts[1]);
                if (!Double.isFinite(from) || !Double.isFinite(to)) {
                    continue;
                }
                count++;
                double delta = Math.abs(to - from) * 100.0;
                if (delta > maxDelta) {
                    maxDelta = delta;
                }
                if (Math.abs(to - from) > 1e-5) {
                    allEqual = false;
                }
            } catch (NumberFormatException ignored) {
            }
        }
        boolean identity = count <= 2 && allEqual;
        return new CurveSummary(count, identity, maxDelta);
    }

    public static NoiseSummary parseNoiseSummary(String rawInfo) {
        if (rawInfo == null || rawInfo.trim().isEmpty()) {
            return null;
        }
        String[] tok = rawInfo.trim().split("\\s+");
        if (tok.length < 6) {
            return null;
        }
        String tag = tok[0];
        int rows;
        int cols;
        try {
            rows = Integer.parseInt(tok[4]);
            cols = Integer.parseInt(tok[5]);
        } catch (NumberFormatException e) {
            return null;
        }
        if ("SPXNOISE1".equals(tag) && tok.length >= 7) {
            int cells = 0;
            try {
                cells = Integer.parseInt(tok[6]);
            } catch (NumberFormatException ignored) {
            }
            return new NoiseSummary(tag, rows, cols, cells, rows * cols, 0, "", "");
        }
        if ("SPXNOISE2".equals(tag) && tok.length >= 8) {
            String axis = tok[6];
            int count = 0;
            try {
                count = Integer.parseInt(tok[7]);
            } catch (NumberFormatException ignored) {
            }
            StringBuilder idx = new StringBuilder();
            for (int i = 8; i < tok.length; i++) {
                if (idx.length() > 0) {
                    idx.append(',');
                }
                idx.append(tok[i]);
            }
            return new NoiseSummary(tag, rows, cols, count, rows * cols, 0, axis, idx.toString());
        }
        if ("SPXNOISE3".equals(tag)) {
            int nz = 0;
            int total = rows * cols;
            int mx = 0;
            for (int i = 6; i < tok.length; i++) {
                if (tok[i].startsWith("nonzero=")) {
                    String[] parts = tok[i].substring(8).split("/");
                    if (parts.length == 2) {
                        try {
                            nz = Integer.parseInt(parts[0]);
                            total = Integer.parseInt(parts[1]);
                        } catch (NumberFormatException ignored) {
                        }
                    }
                } else if (tok[i].startsWith("max=")) {
                    try {
                        mx = Integer.parseInt(tok[i].substring(4));
                    } catch (NumberFormatException ignored) {
                    }
                }
            }
            return new NoiseSummary(tag, rows, cols, nz, total, mx, "", "");
        }
        return null;
    }

    public static TapSummary parseTapSummary(String rawLine) {
        if (rawLine == null || rawLine.trim().isEmpty()) {
            return null;
        }
        String[] tok = rawLine.trim().split("\\s+");
        if (tok.length < 7 || !"SPXTAP1".equals(tok[0])) {
            return null;
        }
        try {
            return new TapSummary(
                    Integer.parseInt(tok[4]),
                    Integer.parseInt(tok[5]),
                    Integer.parseInt(tok[6]));
        } catch (NumberFormatException e) {
            return null;
        }
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
