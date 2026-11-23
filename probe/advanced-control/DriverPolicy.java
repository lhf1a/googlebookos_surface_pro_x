package org.surface.advancedcontrol;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.interfaces.ECPublicKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.ECPoint;
import java.security.spec.X509EncodedKeySpec;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/** Pure policy, sorting, touch summary, and bridge request encoding, shared by the UI and host tests. */
public final class DriverPolicy {
    public static final String APP_PREFIX = "persist.sq2.vulkan.app.";
    public static final String DEFAULT_DRIVER_PROP = "persist.sq2.vulkan.driver";
    public static final int MAX_TOUCH_IMPORT_BYTES = 132096;
    public static final int MAX_SSH_KEY_IMPORT_BYTES = 65536;
    public static final int MAX_SSH_PASSWORD_LENGTH = 128;

    private static final Set<String> SUPPORTED_SSH_KEY_TYPES = Collections.unmodifiableSet(new HashSet<>(Arrays.asList(
            "ssh-ed25519",
            "ssh-rsa",
            "ecdsa-sha2-nistp256",
            "ecdsa-sha2-nistp384",
            "ecdsa-sha2-nistp521",
            "sk-ssh-ed25519@openssh.com",
            "sk-ecdsa-sha2-nistp256@openssh.com"
    )));

    private static final byte[] ED25519_SPKI_PREFIX = new byte[] {
            0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
    };

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

    public static String touchDisabledRequest(String nonce, boolean disabled) {
        return "set-touch-disabled\n" + normalizeNonce(nonce) + "\n" + (disabled ? "1" : "0") + "\n";
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

    public static String adbTcpRequest(String nonce, boolean enabled) {
        return "set-adb-tcp\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String sshEnabledRequest(String nonce, boolean enabled) {
        return "set-ssh-enabled\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String avfVpnShareRequest(String nonce, boolean enabled) {
        return "set-avf-vpn-share\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String sshPasswordRequest(String nonce, String password) {
        if (password == null) {
            throw new IllegalArgumentException("invalid_password");
        }
        if (password.length() > MAX_SSH_PASSWORD_LENGTH) {
            throw new IllegalArgumentException("invalid_password");
        }
        for (int i = 0; i < password.length(); i++) {
            char c = password.charAt(i);
            if (c < 0x20 || c == 0x7f) {
                throw new IllegalArgumentException("invalid_password");
            }
        }
        return "set-ssh-password\n" + normalizeNonce(nonce) + "\n" + password + "\n";
    }

    public static String clearSshKeysRequest(String nonce) {
        return "clear-ssh-keys\n" + normalizeNonce(nonce) + "\n";
    }

    public static String importSshKeysRequest(String nonce, String rawContent) {
        String normalized = normalizeSshKeyImport(rawContent);
        return "import-ssh-keys\n" + normalizeNonce(nonce) + "\n" + normalized;
    }

    public static String normalizeSshKeyImport(String rawContent) {
        if (rawContent == null || rawContent.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        String normalized = rawContent.replace("\r\n", "\n").replace('\r', '\n');
        byte[] bytes = normalized.getBytes(StandardCharsets.UTF_8);
        if (bytes.length == 0 || bytes.length > MAX_SSH_KEY_IMPORT_BYTES) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }

        Map<String, String> dedup = new LinkedHashMap<>();
        String[] lines = normalized.split("\n", -1);
        int blockMode = 0; // 0=none, 1=pem cert, 2=pem spki, 3=rfc4716 ssh2
        StringBuilder blockBuf = new StringBuilder();
        boolean ssh2SkipNextContinuation = false;

        for (String line : lines) {
            String trimmed = line.trim();
            if (blockMode == 0) {
                if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                    continue;
                }
                if ("-----BEGIN CERTIFICATE-----".equals(trimmed)) {
                    blockMode = 1;
                    blockBuf.setLength(0);
                    continue;
                }
                if ("-----BEGIN PUBLIC KEY-----".equals(trimmed)) {
                    blockMode = 2;
                    blockBuf.setLength(0);
                    continue;
                }
                if ("---- BEGIN SSH2 PUBLIC KEY ----".equals(trimmed)) {
                    blockMode = 3;
                    blockBuf.setLength(0);
                    ssh2SkipNextContinuation = false;
                    continue;
                }
                addOpenSshLine(trimmed, dedup);
            } else if (blockMode == 1) {
                if ("-----END CERTIFICATE-----".equals(trimmed)) {
                    addX509CertPem(blockBuf.toString(), dedup);
                    blockMode = 0;
                    blockBuf.setLength(0);
                } else {
                    blockBuf.append(trimmed);
                }
            } else if (blockMode == 2) {
                if ("-----END PUBLIC KEY-----".equals(trimmed)) {
                    addSpkiPem(blockBuf.toString(), dedup);
                    blockMode = 0;
                    blockBuf.setLength(0);
                } else {
                    blockBuf.append(trimmed);
                }
            } else {
                if ("---- END SSH2 PUBLIC KEY ----".equals(trimmed)) {
                    addSsh2Blob(blockBuf.toString(), dedup);
                    blockMode = 0;
                    blockBuf.setLength(0);
                    ssh2SkipNextContinuation = false;
                } else if (ssh2SkipNextContinuation) {
                    ssh2SkipNextContinuation = trimmed.endsWith("\\");
                } else if (trimmed.contains(":")) {
                    ssh2SkipNextContinuation = trimmed.endsWith("\\");
                } else if (!trimmed.isEmpty()) {
                    blockBuf.append(trimmed);
                }
            }
        }

        if (blockMode != 0 || dedup.isEmpty()) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        StringBuilder out = new StringBuilder();
        for (String keyLine : dedup.values()) {
            out.append(keyLine).append('\n');
        }
        return out.toString();
    }

    private static void addOpenSshLine(String line, Map<String, String> dedup) {
        String[] parts = line.split("\\s+", 3);
        if (parts.length < 2) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        String type = parts[0];
        String b64 = parts[1];
        if (!SUPPORTED_SSH_KEY_TYPES.contains(type) || !b64.matches("[A-Za-z0-9+/=]{16,}")) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        byte[] blob;
        try {
            blob = Base64.getDecoder().decode(b64);
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("invalid_ssh_key", e);
        }
        String wireType = readLeadingSshType(blob);
        if (!type.equals(wireType)) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        String comment = parts.length == 3 ? sanitizeComment(parts[2]) : "";
        String formatted = comment.isEmpty() ? (type + " " + b64) : (type + " " + b64 + " " + comment);
        dedup.putIfAbsent(type + " " + b64, formatted);
    }

    private static void addSsh2Blob(String b64Content, Map<String, String> dedup) {
        byte[] blob;
        try {
            blob = Base64.getMimeDecoder().decode(b64Content);
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("invalid_ssh_key", e);
        }
        String wireType = readLeadingSshType(blob);
        if (!SUPPORTED_SSH_KEY_TYPES.contains(wireType)) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        String canonicalB64 = Base64.getEncoder().encodeToString(blob);
        dedup.putIfAbsent(wireType + " " + canonicalB64, wireType + " " + canonicalB64);
    }

    private static void addX509CertPem(String b64Content, Map<String, String> dedup) {
        try {
            byte[] der = Base64.getMimeDecoder().decode(b64Content);
            CertificateFactory cf = CertificateFactory.getInstance("X.509");
            Certificate cert = cf.generateCertificate(new ByteArrayInputStream(der));
            addPublicKeyLine(cert.getPublicKey(), "imported-cert", dedup);
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (Exception e) {
            throw new IllegalArgumentException("invalid_ssh_key", e);
        }
    }

    private static void addSpkiPem(String b64Content, Map<String, String> dedup) {
        byte[] der;
        try {
            der = Base64.getMimeDecoder().decode(b64Content);
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("invalid_ssh_key", e);
        }
        if (isEd25519Spki(der)) {
            String line = encodeEd25519OpenSsh(Arrays.copyOfRange(der, ED25519_SPKI_PREFIX.length, der.length),
                    "imported-cert");
            String[] p = line.split(" ", 3);
            dedup.putIfAbsent(p[0] + " " + p[1], line);
            return;
        }
        X509EncodedKeySpec spec = new X509EncodedKeySpec(der);
        for (String alg : new String[] {"RSA", "EC"}) {
            try {
                PublicKey pk = KeyFactory.getInstance(alg).generatePublic(spec);
                addPublicKeyLine(pk, "imported-cert", dedup);
                return;
            } catch (Exception ignored) {
            }
        }
        throw new IllegalArgumentException("invalid_ssh_key");
    }

    private static void addPublicKeyLine(PublicKey key, String comment, Map<String, String> dedup) throws Exception {
        if (key == null) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        byte[] encoded = key.getEncoded();
        String line;
        if (isEd25519Spki(encoded)) {
            line = encodeEd25519OpenSsh(
                    Arrays.copyOfRange(encoded, ED25519_SPKI_PREFIX.length, encoded.length), comment);
        } else if (key instanceof RSAPublicKey) {
            RSAPublicKey rsa = (RSAPublicKey) key;
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos);
            writeSshString(dos, "ssh-rsa");
            writeSshBytes(dos, rsa.getPublicExponent().toByteArray());
            writeSshBytes(dos, rsa.getModulus().toByteArray());
            dos.flush();
            line = "ssh-rsa " + Base64.getEncoder().encodeToString(baos.toByteArray()) + " " + comment;
        } else if (key instanceof ECPublicKey) {
            ECPublicKey ec = (ECPublicKey) key;
            int bits = ec.getParams().getCurve().getField().getFieldSize();
            String sshType;
            String curveName;
            int coordLen;
            if (bits == 256) {
                sshType = "ecdsa-sha2-nistp256";
                curveName = "nistp256";
                coordLen = 32;
            } else if (bits == 384) {
                sshType = "ecdsa-sha2-nistp384";
                curveName = "nistp384";
                coordLen = 48;
            } else if (bits == 521) {
                sshType = "ecdsa-sha2-nistp521";
                curveName = "nistp521";
                coordLen = 66;
            } else {
                throw new IllegalArgumentException("invalid_ssh_key");
            }
            ECPoint w = ec.getW();
            byte[] point = new byte[1 + 2 * coordLen];
            point[0] = 0x04;
            copyFixedUnsigned(w.getAffineX(), point, 1, coordLen);
            copyFixedUnsigned(w.getAffineY(), point, 1 + coordLen, coordLen);
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos);
            writeSshString(dos, sshType);
            writeSshString(dos, curveName);
            writeSshBytes(dos, point);
            dos.flush();
            line = sshType + " " + Base64.getEncoder().encodeToString(baos.toByteArray()) + " " + comment;
        } else {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        String[] p = line.split(" ", 3);
        dedup.putIfAbsent(p[0] + " " + p[1], line);
    }

    private static boolean isEd25519Spki(byte[] der) {
        if (der == null || der.length != ED25519_SPKI_PREFIX.length + 32) {
            return false;
        }
        for (int i = 0; i < ED25519_SPKI_PREFIX.length; i++) {
            if (der[i] != ED25519_SPKI_PREFIX[i]) {
                return false;
            }
        }
        return true;
    }

    private static String encodeEd25519OpenSsh(byte[] raw32, String comment) {
        try {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos);
            writeSshString(dos, "ssh-ed25519");
            writeSshBytes(dos, raw32);
            dos.flush();
            return "ssh-ed25519 " + Base64.getEncoder().encodeToString(baos.toByteArray()) + " " + comment;
        } catch (Exception e) {
            throw new IllegalArgumentException("invalid_ssh_key", e);
        }
    }

    private static void copyFixedUnsigned(BigInteger value, byte[] dst, int offset, int len) {
        byte[] raw = value.toByteArray();
        int srcOff = 0;
        int srcLen = raw.length;
        if (srcLen > len && raw[0] == 0) {
            srcOff = 1;
            srcLen--;
        }
        if (srcLen > len) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        System.arraycopy(raw, srcOff, dst, offset + (len - srcLen), srcLen);
    }

    private static void writeSshString(DataOutputStream dos, String ascii) throws Exception {
        writeSshBytes(dos, ascii.getBytes(StandardCharsets.US_ASCII));
    }

    private static void writeSshBytes(DataOutputStream dos, byte[] data) throws Exception {
        dos.writeInt(data.length);
        dos.write(data);
    }

    private static String readLeadingSshType(byte[] blob) {
        if (blob == null || blob.length < 8) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        int len = ((blob[0] & 0xFF) << 24)
                | ((blob[1] & 0xFF) << 16)
                | ((blob[2] & 0xFF) << 8)
                | (blob[3] & 0xFF);
        if (len <= 0 || len > 64 || 4 + len > blob.length) {
            throw new IllegalArgumentException("invalid_ssh_key");
        }
        return new String(blob, 4, len, StandardCharsets.US_ASCII);
    }

    private static String sanitizeComment(String raw) {
        String trimmed = raw.trim();
        for (int i = 0; i < trimmed.length(); i++) {
            char c = trimmed.charAt(i);
            if (c < 0x20 || c == 0x7f) {
                throw new IllegalArgumentException("invalid_ssh_key");
            }
        }
        return trimmed;
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

    public static final String FACE_ENABLED_PROP = "persist.spx.face.enabled";
    public static final String FACE_ROTATION_PROP = "persist.spx.face.rotation";
    public static final String[] FACE_ROTATIONS = new String[] {"0", "90", "180", "270"};

    public static boolean isValidRotation(String raw) {
        if (raw == null) {
            return false;
        }
        String t = raw.trim();
        return "0".equals(t) || "90".equals(t) || "180".equals(t) || "270".equals(t);
    }

    public static String validateRotation(String raw) {
        if (!isValidRotation(raw)) {
            throw new IllegalArgumentException("invalid_rotation");
        }
        return raw.trim();
    }

    public static String setHallLidRequest(String nonce, boolean enabled) {
        return "set-hall-lid\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String setFaceUnlockRequest(String nonce, boolean enabled) {
        return "set-face-unlock\n" + normalizeNonce(nonce) + "\n" + (enabled ? "1" : "0") + "\n";
    }

    public static String setFaceRotationRequest(String nonce, String rotation) {
        return "set-face-rotation\n" + normalizeNonce(nonce) + "\n" + validateRotation(rotation) + "\n";
    }

    public static String faceSelfCheckRequest(String nonce) {
        return "run-face-self-check\n" + normalizeNonce(nonce) + "\n";
    }

    public static Map<String, String> parseSelfCheckSummary(String raw) {
        Map<String, String> out = new java.util.LinkedHashMap<>();
        if (raw == null) {
            return out;
        }
        for (String token : raw.trim().split(";")) {
            int eq = token.indexOf('=');
            if (eq > 0) {
                out.put(token.substring(0, eq).trim(), token.substring(eq + 1).trim());
            }
        }
        return out;
    }
}
