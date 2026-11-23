package org.surface.advancedcontrol;

import android.content.Context;
import android.os.SystemClock;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

/** File-based IPC client talking to the built-in initramfs root bridge daemon. */
final class BridgeClient {
    static final class BridgeState {
        boolean bridgeReady;
        boolean vulkanAvailable;
        String vulkanDefault = "";
        String policyDefault = "turnip";
        List<String> availableGovernors = new ArrayList<>();
        String currentGovernor = "";
        String savedGovernor = "";
        String cpuPolicies = "";
        boolean touchAvailable;
        boolean touchIptsdRunning;
        boolean touchDisabled;
        String touchDeviceId = "";
        String touchHidId = "";
        boolean touchProfileSaved;
        String touchProfileX = "";
        String touchProfileY = "";
        String touchProfileXTop = "";
        String touchProfileTopStart = "";
        String touchProfileTopEnd = "";
        String touchProfileSizeMax = "";
        boolean touchNoiseSaved;
        boolean touchNoiseEnabled;
        String touchNoiseInfo = "";
        boolean touchTapSaved;
        String touchTapLine = "";
        boolean adbTcpEnabled;
        boolean adbTcpActive;
        String adbTcpPort = "";
        boolean lowerUsbAvailable;
        String lowerUsbMode = "auto";
        String lowerUsbRole = "";
        String lowerUsbDpState = "none";
        boolean lowerUsbBootLocked;
        boolean lowerUsbWatchRunning;
        boolean sshAvailable;
        boolean sshEnabled;
        boolean sshRunning;
        String sshPort = "22";
        boolean sshPasswordSet;
        int sshKeyCount;
        boolean avfVpnAvailable;
        boolean avfVpnEnabled;
        boolean avfVpnRunning;
        boolean avfVpnActive;
        String debugVmIp = "";
        String debugLanIps = "";
        boolean hallLidAvailable;
        boolean hallLidEnabled;
        String hallLidState = "UNAVAILABLE";
        String ssamCoverState = "unavailable";
        boolean faceIrAvailable;
        boolean faceUnlockEnabled = true;
        boolean faceHalRunning;
        String faceIrRotation = "270";
    }

    static final class TouchImportResult {
        final BridgeState state;
        final String kind;

        TouchImportResult(BridgeState state, String kind) {
            this.state = state;
            this.kind = kind;
        }
    }

    static final class SshKeyImportResult {
        final BridgeState state;
        final int added;
        final int total;

        SshKeyImportResult(BridgeState state, int added, int total) {
            this.state = state;
            this.added = added;
            this.total = total;
        }
    }

    private final File bridgeDir;
    private final File cacheDir;

    BridgeClient(Context context) {
        this.bridgeDir = context.createDeviceProtectedStorageContext().getFilesDir();
        this.cacheDir = context.getCacheDir();
        if (!bridgeDir.isDirectory()) {
            bridgeDir.mkdirs();
        }
    }

    Map<String, String> readSavedAppDrivers() throws Exception {
        Map<String, String> values = new HashMap<>();
        String output = runReadOnly("/system/bin/getprop");
        for (String line : output.split("\n")) {
            int split = line.indexOf("]: [");
            if (line.startsWith("[" + DriverPolicy.APP_PREFIX) && split > 0 && line.endsWith("]")) {
                String key = line.substring(1, split);
                String val = line.substring(split + 4, line.length() - 1);
                values.put(key, val);
            }
        }
        return values;
    }

    BridgeState loadState(boolean requestRefresh) {
        boolean bridgeOk = false;
        if (requestRefresh) {
            try {
                sendRequest(DriverPolicy.refreshRequest(newNonce()), 3500);
                bridgeOk = true;
            } catch (Exception ignored) {
                bridgeOk = false;
            }
        }

        BridgeState state = new BridgeState();
        File stateFile = new File(bridgeDir, "state");
        if (stateFile.isFile()) {
            try {
                String raw = new String(Files.readAllBytes(stateFile.toPath()), StandardCharsets.UTF_8);
                parseStateInto(raw, state);
                if (bridgeOk || state.bridgeReady) {
                    state.bridgeReady = true;
                }
            } catch (Exception ignored) {
            }
        }

        // Non-root fallbacks keep read-only display informative even if bridge is unavailable.
        if (!state.bridgeReady) {
            fillLocalFallback(state);
        }
        return state;
    }

    BridgeState setAppVulkan(int uid, String pkg, String driver, boolean stop) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.appVulkanRequest(nonce, uid, pkg, driver, stop), 15000);
        return loadState(false);
    }

    BridgeState setDefaultVulkan(String driver) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.defaultVulkanRequest(nonce, driver), 15000);
        return loadState(false);
    }

    BridgeState setCpuGovernor(String governor) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.cpuGovernorRequest(nonce, governor), 15000);
        return loadState(false);
    }

    BridgeState setTouchDisabled(boolean disabled) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.touchDisabledRequest(nonce, disabled), 25000);
        return loadState(false);
    }

    BridgeState setTouchGhostPatch(boolean enabled) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.touchGhostPatchRequest(nonce, enabled), 20000);
        return loadState(false);
    }

    BridgeState clearTouchCalibration() throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.clearTouchCalibrationRequest(nonce), 25000);
        return loadState(false);
    }

    TouchImportResult importTouchCalibration(String rawContent) throws Exception {
        String nonce = newNonce();
        String expectedKind = DriverPolicy.detectTouchImportKind(rawContent);
        String appliedKind = sendRequest(DriverPolicy.importTouchCalibrationRequest(nonce, rawContent), 25000).trim();
        String kind = appliedKind.isEmpty() ? expectedKind : appliedKind;
        return new TouchImportResult(loadState(false), kind);
    }

    String exportTouchCalibration() throws Exception {
        String nonce = newNonce();
        String content = sendRequest(DriverPolicy.exportTouchCalibrationRequest(nonce), 15000);
        if (content == null || content.trim().isEmpty()) {
            throw new IOException("empty_export");
        }
        return content;
    }

    BridgeState setAdbTcp(boolean enabled) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.adbTcpRequest(nonce, enabled), 15000);
        return loadState(false);
    }

    BridgeState setLowerUsbMode(String mode) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.lowerUsbModeRequest(nonce, mode), 20000);
        return loadState(false);
    }

    BridgeState setSshEnabled(boolean enabled) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.sshEnabledRequest(nonce, enabled), 15000);
        return loadState(false);
    }

    BridgeState setAvfVpnShare(boolean enabled) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.avfVpnShareRequest(nonce, enabled), 15000);
        return loadState(false);
    }

    BridgeState setSshPassword(String password) throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.sshPasswordRequest(nonce, password), 15000);
        return loadState(false);
    }

    SshKeyImportResult importSshKeys(String rawContent) throws Exception {
        String nonce = newNonce();
        String payload = sendRequest(DriverPolicy.importSshKeysRequest(nonce, rawContent), 15000).trim();
        int added = 0;
        int total = 0;
        if (!payload.isEmpty()) {
            String[] parts = payload.split("\\s+");
            try {
                added = Integer.parseInt(parts[0]);
                if (parts.length > 1) {
                    total = Integer.parseInt(parts[1]);
                }
            } catch (NumberFormatException ignored) {
            }
        }
        BridgeState next = loadState(false);
        return new SshKeyImportResult(next, added, total > 0 ? total : next.sshKeyCount);
    }

    BridgeState clearSshKeys() throws Exception {
        String nonce = newNonce();
        sendRequest(DriverPolicy.clearSshKeysRequest(nonce), 15000);
        return loadState(false);
    }

    private String sendRequest(String body, long timeoutMs) throws Exception {
        if (!bridgeDir.isDirectory() && !bridgeDir.mkdirs()) {
            throw new IOException("Bridge directory unavailable");
        }
        String[] lines = body.split("\n");
        if (lines.length < 2) {
            throw new IllegalArgumentException("Malformed bridge request");
        }
        String nonce = lines[1];
        File tmp = new File(bridgeDir, "request.tmp");
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            fos.write(body.getBytes(StandardCharsets.UTF_8));
            fos.getFD().sync();
        }
        File target = new File(bridgeDir, "request");
        if (!tmp.renameTo(target)) {
            tmp.delete();
            throw new IOException("Unable to submit bridge request");
        }

        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        File replyFile = new File(bridgeDir, "reply");
        while (SystemClock.elapsedRealtime() < deadline) {
            if (replyFile.isFile()) {
                try {
                    String raw = new String(Files.readAllBytes(replyFile.toPath()), StandardCharsets.UTF_8);
                    int firstNl = raw.indexOf('\n');
                    if (firstNl > 0 && nonce.equals(raw.substring(0, firstNl).trim())) {
                        int secondNl = raw.indexOf('\n', firstNl + 1);
                        String status = (secondNl >= 0
                                ? raw.substring(firstNl + 1, secondNl)
                                : raw.substring(firstNl + 1)).trim();
                        String payload = secondNl >= 0 ? raw.substring(secondNl + 1) : "";
                        if ("OK".equals(status)) {
                            return payload;
                        }
                        String detail = payload.trim();
                        throw new IOException(detail.isEmpty() ? (status.isEmpty() ? "bridge_error" : status) : detail);
                    }
                } catch (IOException e) {
                    if (e.getMessage() != null && !e.getMessage().isEmpty()
                            && !e.getMessage().contains("No such file")) {
                        throw e;
                    }
                }
            }
            SystemClock.sleep(150);
        }
        throw new IOException("bridge_timeout");
    }

    private static void parseStateInto(String raw, BridgeState state) {
        Map<String, String> map = new HashMap<>();
        for (String line : raw.split("\n")) {
            int eq = line.indexOf('=');
            if (eq > 0) {
                map.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        }
        if ("1".equals(map.get("bridge_version"))) {
            state.bridgeReady = true;
        }
        state.vulkanAvailable = "1".equals(map.get("vulkan_available"));
        state.vulkanDefault = map.getOrDefault("vulkan_default", "");
        state.policyDefault = map.getOrDefault("policy_default", "turnip");
        state.availableGovernors = DriverPolicy.parseAvailableGovernors(map.get("cpu_available_governors"));
        state.currentGovernor = map.getOrDefault("cpu_current_governor", "");
        state.savedGovernor = map.getOrDefault("cpu_saved_governor", "");
        state.cpuPolicies = map.getOrDefault("cpu_policies", "");
        state.touchAvailable = "1".equals(map.get("touch_available"));
        state.touchIptsdRunning = "1".equals(map.get("touch_iptsd_running"));
        state.touchDisabled = "1".equals(map.get("touch_disabled"));
        state.touchDeviceId = map.getOrDefault("touch_device_id", "");
        state.touchHidId = map.getOrDefault("touch_hid_id", "");
        state.touchProfileSaved = "1".equals(map.get("touch_profile_saved"));
        state.touchProfileX = map.getOrDefault("touch_profile_x", "");
        state.touchProfileY = map.getOrDefault("touch_profile_y", "");
        state.touchProfileXTop = map.getOrDefault("touch_profile_x_top", "");
        state.touchProfileTopStart = map.getOrDefault("touch_profile_top_start", "");
        state.touchProfileTopEnd = map.getOrDefault("touch_profile_top_end", "");
        state.touchProfileSizeMax = map.getOrDefault("touch_profile_size_max", "");
        state.touchNoiseSaved = "1".equals(map.get("touch_noise_saved"));
        state.touchNoiseEnabled = "1".equals(map.get("touch_noise_enabled"));
        state.touchNoiseInfo = map.getOrDefault("touch_noise_info", "");
        state.touchTapSaved = "1".equals(map.get("touch_tap_saved"));
        state.touchTapLine = map.getOrDefault("touch_tap_line", "");
        state.adbTcpEnabled = "1".equals(map.get("adb_tcp_enabled"));
        state.adbTcpActive = "1".equals(map.get("adb_tcp_active"));
        state.adbTcpPort = map.getOrDefault("adb_tcp_port", "");
        state.lowerUsbAvailable = "1".equals(map.get("lower_usb_available"));
        String luMode = map.getOrDefault("lower_usb_mode", "auto");
        state.lowerUsbMode = ("auto".equals(luMode) || "adb".equals(luMode) || "host".equals(luMode)) ? luMode : "auto";
        state.lowerUsbRole = map.getOrDefault("lower_usb_role", "");
        String luDp = map.getOrDefault("lower_usb_dp_state", "none");
        state.lowerUsbDpState = ("active".equals(luDp) || "pending".equals(luDp)) ? luDp : "none";
        state.lowerUsbBootLocked = "1".equals(map.get("lower_usb_boot_locked"));
        state.lowerUsbWatchRunning = "1".equals(map.get("lower_usb_watch_running"));
        state.sshAvailable = "1".equals(map.get("ssh_available"));
        state.sshEnabled = "1".equals(map.get("ssh_enabled"));
        state.sshRunning = "1".equals(map.get("ssh_running"));
        String port = map.getOrDefault("ssh_port", "22");
        state.sshPort = port.isEmpty() ? "22" : port;
        state.sshPasswordSet = "1".equals(map.get("ssh_password_set"));
        try {
            state.sshKeyCount = Integer.parseInt(map.getOrDefault("ssh_key_count", "0"));
        } catch (NumberFormatException e) {
            state.sshKeyCount = 0;
        }
        state.avfVpnAvailable = "1".equals(map.get("avf_vpn_available"));
        state.avfVpnEnabled = "1".equals(map.get("avf_vpn_enabled"));
        state.avfVpnRunning = "1".equals(map.get("avf_vpn_running"));
        state.avfVpnActive = "1".equals(map.get("avf_vpn_active"));
        state.debugVmIp = map.getOrDefault("debug_vm_ip", "");
        state.debugLanIps = map.getOrDefault("debug_lan_ips", "");
        state.hallLidAvailable = "1".equals(map.get("hall_lid_available"));
        state.hallLidEnabled = "1".equals(map.get("hall_lid_enabled"));
        String lidState = map.getOrDefault("hall_lid_state", "UNAVAILABLE");
        state.hallLidState = lidState.isEmpty() ? "UNAVAILABLE" : lidState;
        String cover = map.getOrDefault("ssam_cover_state", "unavailable");
        state.ssamCoverState = cover.isEmpty() ? "unavailable" : cover;
        state.faceIrAvailable = "1".equals(map.get("face_ir_available"));
        String faceEn = map.get("face_unlock_enabled");
        state.faceUnlockEnabled = faceEn == null ? state.faceIrAvailable : "1".equals(faceEn);
        state.faceHalRunning = "1".equals(map.get("face_hal_running"));
        String rot = map.getOrDefault("face_ir_rotation", "270");
        state.faceIrRotation = DriverPolicy.isValidRotation(rot) ? rot : "270";
    }

    static final class FaceSelfCheckResult {
        final BridgeState state;
        final String summary;

        FaceSelfCheckResult(BridgeState state, String summary) {
            this.state = state;
            this.summary = summary;
        }
    }

    BridgeState setHallLid(boolean enabled) throws Exception {
        sendRequest(DriverPolicy.setHallLidRequest(newNonce(), enabled), 15000);
        return loadState(false);
    }

    BridgeState setFaceUnlock(boolean enabled) throws Exception {
        sendRequest(DriverPolicy.setFaceUnlockRequest(newNonce(), enabled), 15000);
        return loadState(false);
    }

    FaceSelfCheckResult runFaceSelfCheck() throws Exception {
        String payload = sendRequest(DriverPolicy.faceSelfCheckRequest(newNonce()), 25000);
        BridgeState state = loadState(false);
        return new FaceSelfCheckResult(state, payload != null ? payload.trim() : "");
    }

    private void fillLocalFallback(BridgeState state) {
        state.vulkanAvailable = new File("/vendor/lib64/hw/vulkan.turnip.so").isFile()
                && new File("/vendor/lib64/hw/vulkan.qti.so").isFile()
                && new File("/vendor/etc/surface-vulkan-app-routing.version").isFile();
        try {
            state.vulkanDefault = runReadOnly("/system/bin/getprop", DriverPolicy.DEFAULT_DRIVER_PROP).trim();
        } catch (Exception ignored) {
        }
        File cpufreqDir = new File("/sys/devices/system/cpu/cpufreq");
        File[] children = cpufreqDir.listFiles();
        if (children != null) {
            Arrays.sort(children, Comparator.comparing(File::getName));
            List<String> common = null;
            String current = "";
            boolean mixed = false;
            StringBuilder pols = new StringBuilder();
            for (File child : children) {
                if (!child.isDirectory() || !child.getName().startsWith("policy")) {
                    continue;
                }
                File availFile = new File(child, "scaling_available_governors");
                File curFile = new File(child, "scaling_governor");
                if (!curFile.isFile()) {
                    continue;
                }
                try {
                    if (availFile.isFile()) {
                        List<String> avail = DriverPolicy.parseAvailableGovernors(
                                new String(Files.readAllBytes(availFile.toPath()), StandardCharsets.UTF_8));
                        if (common == null) {
                            common = new ArrayList<>(avail);
                        } else {
                            common.retainAll(avail);
                        }
                    }
                    String gov = new String(Files.readAllBytes(curFile.toPath()), StandardCharsets.UTF_8).trim();
                    if (pols.length() > 0) {
                        pols.append(',');
                    }
                    pols.append(child.getName()).append(':').append(gov);
                    if (current.isEmpty()) {
                        current = gov;
                    } else if (!current.equals(gov)) {
                        mixed = true;
                    }
                } catch (Exception ignored) {
                }
            }
            if (common != null) {
                state.availableGovernors = common;
            }
            state.currentGovernor = mixed ? "mixed" : current;
            state.cpuPolicies = pols.toString();
        }
        File lidDir = new File("/sys/devices/platform/gpio-keys-lid");
        if (lidDir.isDirectory()) {
            state.hallLidAvailable = true;
            state.hallLidEnabled = true;
            File disFile = new File(lidDir, "disabled_switches");
            if (disFile.isFile()) {
                try {
                    String dis = new String(Files.readAllBytes(disFile.toPath()), StandardCharsets.UTF_8).trim();
                    for (String tok : dis.split(",")) {
                        if ("0".equals(tok) || tok.startsWith("0-")) {
                            state.hallLidEnabled = false;
                            break;
                        }
                    }
                } catch (Exception ignored) {
                }
            }
            state.hallLidState = state.hallLidEnabled ? "OPEN" : "DISABLED";
        }
        File ovDir = new File("/sys/bus/i2c/drivers/ov7251");
        File[] ovEntries = ovDir.listFiles();
        if (ovEntries != null) {
            for (File f : ovEntries) {
                if (f.getName().contains("-")) {
                    state.faceIrAvailable = true;
                    break;
                }
            }
        }
        try {
            String fe = runReadOnly("/system/bin/getprop", DriverPolicy.FACE_ENABLED_PROP).trim();
            state.faceUnlockEnabled = !"0".equals(fe);
        } catch (Exception ignored) {
        }
        try {
            String fr = runReadOnly("/system/bin/getprop", DriverPolicy.FACE_ROTATION_PROP).trim();
            if (DriverPolicy.isValidRotation(fr)) {
                state.faceIrRotation = fr;
            }
        } catch (Exception ignored) {
        }
        try {
            String svc = runReadOnly("/system/bin/getprop", "init.svc.vendor.face-default").trim();
            state.faceHalRunning = "running".equals(svc);
        } catch (Exception ignored) {
        }
    }

    private String runReadOnly(String... args) throws Exception {
        File output = File.createTempFile("ac-read-", ".txt", cacheDir);
        Process child = null;
        try {
            child = new ProcessBuilder(args).redirectErrorStream(true).redirectOutput(output).start();
            if (!child.waitFor(10, TimeUnit.SECONDS)) {
                throw new IOException("read_timeout");
            }
            String text = new String(Files.readAllBytes(output.toPath()), StandardCharsets.UTF_8);
            if (child.exitValue() != 0) {
                throw new IOException("exit=" + child.exitValue());
            }
            return text;
        } finally {
            if (child != null && child.isAlive()) {
                child.destroyForcibly();
            }
            output.delete();
        }
    }

    private static String newNonce() {
        return UUID.randomUUID().toString().toLowerCase(Locale.ROOT);
    }
}
