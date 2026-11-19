package org.surface.advancedcontrol;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Pure policy, validation, and IPC message builders for Surface System Update (OTA). */
public final class OtaPolicy {
    public static final String STAGED_UPLOAD_NAME = "ota-upload.pkg";

    private OtaPolicy() {}

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

    public static String validateSha256(String sha) {
        if (sha == null || sha.length() != 64) {
            throw new IllegalArgumentException("Invalid SHA-256 length");
        }
        for (int i = 0; i < sha.length(); i++) {
            char c = sha.charAt(i);
            boolean ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!ok) {
                throw new IllegalArgumentException("Invalid SHA-256 character");
            }
        }
        return sha;
    }

    public static String validateSafeRelPath(String rel) {
        if (rel == null || rel.isEmpty() || rel.length() > 256) {
            throw new IllegalArgumentException("Invalid relative path");
        }
        if (rel.startsWith("/") || rel.endsWith("/") || rel.equals("..")
                || rel.startsWith("../") || rel.endsWith("/..") || rel.contains("/../")
                || rel.contains("\\") || rel.contains("\n") || rel.contains("\r")
                || rel.contains("'") || rel.contains("\"") || rel.contains("$") || rel.contains("`")) {
            throw new IllegalArgumentException("Unsafe relative path");
        }
        return rel;
    }

    public static String refreshRequest(String nonce) {
        return "ota-refresh\n" + validateNonce(nonce) + "\n";
    }

    public static String inspectRequest(String nonce, String relPath) {
        return "ota-inspect\n" + validateNonce(nonce) + "\n" + validateSafeRelPath(relPath) + "\n";
    }

    public static String installRequest(String nonce, String sha256, boolean wipeData) {
        return "ota-install\n" + validateNonce(nonce) + "\n" + validateSha256(sha256)
                + "\n" + (wipeData ? "1" : "0") + "\n";
    }

    public static String cancelRequest(String nonce) {
        return "ota-cancel\n" + validateNonce(nonce) + "\n";
    }

    public static String clearResultRequest(String nonce) {
        return "ota-clear-result\n" + validateNonce(nonce) + "\n";
    }

    public static String rebootRequest(String nonce) {
        return "ota-reboot\n" + validateNonce(nonce) + "\n";
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
        double kb = bytes / 1024.0;
        if (kb < 1024.0) {
            return String.format(Locale.US, "%.1f KiB", kb);
        }
        double mb = kb / 1024.0;
        if (mb < 1024.0) {
            return String.format(Locale.US, "%.1f MiB", mb);
        }
        double gb = mb / 1024.0;
        return String.format(Locale.US, "%.2f GiB", gb);
    }

    public static String shortSha256(String sha) {
        if (sha == null || sha.length() < 16) {
            return sha == null ? "" : sha;
        }
        return sha.substring(0, 12) + "…" + sha.substring(sha.length() - 8);
    }
}
