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

    private void sendRequest(String body, long timeoutMs) throws Exception {
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
                    String[] replyLines = raw.split("\n");
                    if (replyLines.length >= 2 && nonce.equals(replyLines[0].trim())) {
                        String status = replyLines[1].trim();
                        if ("OK".equals(status)) {
                            return;
                        }
                        String detail = replyLines.length >= 3 ? replyLines[2].trim() : status;
                        throw new IOException(detail.isEmpty() ? "bridge_error" : detail);
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
