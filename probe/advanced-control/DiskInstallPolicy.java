package org.surface.advancedcontrol;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Pure policy, validation, state parsing, and IPC request builders for Internal Disk Install. */
public final class DiskInstallPolicy {
    public static final class TargetDisk {
        public final String device;
        public final String name;
        public final String model;
        public final long bytes;
        public final String bus;
        public final boolean enoughSpace;
        public final int mountedParts;

        public TargetDisk(String device, String name, String model, long bytes,
                          String bus, boolean enoughSpace, int mountedParts) {
            this.device = device;
            this.name = name;
            this.model = model;
            this.bytes = bytes;
            this.bus = bus;
            this.enoughSpace = enoughSpace;
            this.mountedParts = mountedParts;
        }
    }

    public static final class DiskState {
        public boolean bridgeReady;
        public String bootDisk = "";
        public String bootTransport = "";
        public boolean bootIsUsb;
        public boolean layoutCompatible;
        public String layoutReason = "";
        public long requiredSuperBytes;
        public long requiredMetadataBytes;
        public long requiredMiscBytes;
        public long requiredEspBytes;
        public long minTargetBytes;
        public boolean eligible;
        public String ineligibleReason = "";
        public final List<TargetDisk> targets = new ArrayList<>();
    }

    public static final class InstallResult {
        public String targetDev = "";
        public String superSha256 = "";
        public long superBytes;
        public long userdataBytes;
    }

    private DiskInstallPolicy() {}

    public static String validateNonce(String nonce) {
        if (nonce == null || nonce.isEmpty() || nonce.length() > 64) {
            throw new IllegalArgumentException("Invalid nonce");
        }
        for (int i = 0; i < nonce.length(); i++) {
            char c = nonce.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!ok) {
                throw new IllegalArgumentException("Invalid nonce character");
            }
        }
        return nonce;
    }

    public static String validateTargetDevice(String device) {
        if (device == null || !device.startsWith("/dev/") || device.length() <= 5 || device.length() > 64) {
            throw new IllegalArgumentException("Invalid target device");
        }
        String name = device.substring(5);
        if (name.startsWith(".") || name.contains("..") || name.contains("/")) {
            throw new IllegalArgumentException("Unsafe target device");
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                throw new IllegalArgumentException("Invalid target device character");
            }
        }
        return device;
    }

    public static String refreshRequest(String nonce) {
        return "disk-refresh\n" + validateNonce(nonce) + "\n";
    }

    public static String installRequest(String nonce, String targetDev) {
        return "disk-install\n" + validateNonce(nonce) + "\n" + validateTargetDevice(targetDev) + "\n";
    }

    public static String poweroffRequest(String nonce) {
        return "disk-poweroff\n" + validateNonce(nonce) + "\n";
    }

    public static Map<String, String> parseKeyValues(String raw) {
        Map<String, String> map = new HashMap<>();
        if (raw == null || raw.isEmpty()) {
            return map;
        }
        for (String line : raw.split("\n")) {
            int eq = line.indexOf('=');
            if (eq > 0) {
                String k = line.substring(0, eq).trim();
                String v = line.substring(eq + 1).replace("\r", "").trim();
                map.put(k, v);
            }
        }
        return map;
    }

    public static DiskState parseState(String raw) {
        Map<String, String> map = parseKeyValues(raw);
        DiskState s = new DiskState();
        s.bridgeReady = "1".equals(map.get("bridge_ready")) || "1".equals(map.get("disk_install_version"));
        s.bootDisk = map.getOrDefault("boot_disk", "");
        s.bootTransport = map.getOrDefault("boot_transport", "");
        s.bootIsUsb = "1".equals(map.get("boot_is_usb"));
        s.layoutCompatible = "1".equals(map.get("layout_compatible"));
        s.layoutReason = map.getOrDefault("layout_reason", "");
        s.requiredSuperBytes = parseLongOrZero(map.get("required_super_bytes"));
        s.requiredMetadataBytes = parseLongOrZero(map.get("required_metadata_bytes"));
        s.requiredMiscBytes = parseLongOrZero(map.get("required_misc_bytes"));
        s.requiredEspBytes = parseLongOrZero(map.get("required_esp_bytes"));
        s.minTargetBytes = parseLongOrZero(map.get("min_target_bytes"));
        s.eligible = "1".equals(map.get("eligible"));
        s.ineligibleReason = map.getOrDefault("ineligible_reason", "");

        int count = Math.min(16, Math.max(0, parseIntOrZero(map.get("target_count"))));
        for (int i = 0; i < count; i++) {
            String prefix = "target_" + i + "_";
            String dev = map.getOrDefault(prefix + "dev", "");
            if (dev.isEmpty()) {
                continue;
            }
            s.targets.add(new TargetDisk(
                    dev,
                    map.getOrDefault(prefix + "name", dev),
                    map.getOrDefault(prefix + "model", dev),
                    parseLongOrZero(map.get(prefix + "bytes")),
                    map.getOrDefault(prefix + "bus", "internal"),
                    "1".equals(map.get(prefix + "enough_space")),
                    parseIntOrZero(map.get(prefix + "mounted_parts"))));
        }
        return s;
    }

    public static InstallResult parseInstallResult(String raw) {
        Map<String, String> map = parseKeyValues(raw);
        InstallResult r = new InstallResult();
        r.targetDev = map.getOrDefault("TARGET_DEV", "");
        r.superSha256 = map.getOrDefault("SUPER_SHA256", "");
        r.superBytes = parseLongOrZero(map.get("SUPER_BYTES"));
        r.userdataBytes = parseLongOrZero(map.get("USERDATA_BYTES"));
        return r;
    }

    public static long parseLongOrZero(String s) {
        if (s == null || s.isEmpty()) {
            return 0L;
        }
        try {
            return Long.parseLong(s.trim());
        } catch (NumberFormatException ignored) {
            return 0L;
        }
    }

    public static int parseIntOrZero(String s) {
        if (s == null || s.isEmpty()) {
            return 0;
        }
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    public static String formatBytes(long bytes) {
        if (bytes < 0) {
            return "0 B";
        }
        if (bytes < 1024L) {
            return bytes + " B";
        }
        double mb = bytes / (1024.0 * 1024.0);
        if (mb < 1024.0) {
            return String.format(Locale.US, "%.0f MiB", mb);
        }
        double gb = mb / 1024.0;
        if (gb < 100.0) {
            return String.format(Locale.US, "%.2f GiB", gb);
        }
        return String.format(Locale.US, "%.1f GiB", gb);
    }
}
