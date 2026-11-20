package org.surface.advancedcontrol;

import android.content.Context;
import android.os.SystemClock;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

/** File-based IPC client talking to /bin/spx-ota-boot for fresh internal-disk installation. */
final class DiskInstallClient {
    interface ProgressListener {
        void onProgress(String stage, int percent, String detail);
    }

    private final File bridgeDir;

    DiskInstallClient(Context context) {
        this.bridgeDir = context.createDeviceProtectedStorageContext().getFilesDir();
        if (!bridgeDir.isDirectory()) {
            bridgeDir.mkdirs();
        }
    }

    DiskInstallPolicy.DiskState loadState(boolean requestRefresh) {
        boolean bridgeOk = false;
        if (requestRefresh) {
            try {
                sendRequest(DiskInstallPolicy.refreshRequest(newNonce()), 10000L, null);
                bridgeOk = true;
            } catch (Exception ignored) {
                bridgeOk = false;
            }
        }
        File stateFile = new File(bridgeDir, "disk-state");
        if (stateFile.isFile()) {
            try {
                String raw = new String(Files.readAllBytes(stateFile.toPath()), StandardCharsets.UTF_8);
                DiskInstallPolicy.DiskState s = DiskInstallPolicy.parseState(raw);
                if (bridgeOk) {
                    s.bridgeReady = true;
                }
                return s;
            } catch (Exception ignored) {
            }
        }
        return new DiskInstallPolicy.DiskState();
    }

    DiskInstallPolicy.InstallResult installToDisk(String targetDev, ProgressListener listener) throws Exception {
        String payload = sendRequest(
                DiskInstallPolicy.installRequest(newNonce(), targetDev),
                1800000L,
                listener);
        return DiskInstallPolicy.parseInstallResult(payload);
    }

    void requestPowerOff() throws Exception {
        sendRequest(DiskInstallPolicy.poweroffRequest(newNonce()), 10000L, null);
    }

    private String sendRequest(String body, long timeoutMs, ProgressListener listener) throws Exception {
        if (!bridgeDir.isDirectory() && !bridgeDir.mkdirs()) {
            throw new IOException("bridge_dir_unavailable");
        }
        String[] lines = body.split("\n");
        if (lines.length < 2) {
            throw new IllegalArgumentException("Malformed disk-install request");
        }
        String nonce = lines[1];
        File progressFile = new File(bridgeDir, "disk-progress");
        if (progressFile.isFile()) {
            progressFile.delete();
        }
        File tmp = new File(bridgeDir, "disk-request.tmp");
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            fos.write(body.getBytes(StandardCharsets.UTF_8));
            fos.getFD().sync();
        }
        File target = new File(bridgeDir, "disk-request");
        if (!tmp.renameTo(target)) {
            tmp.delete();
            throw new IOException("unable_to_submit_request");
        }

        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        File replyFile = new File(bridgeDir, "disk-reply");
        String lastProgressRaw = "";
        while (SystemClock.elapsedRealtime() < deadline) {
            if (listener != null && progressFile.isFile()) {
                try {
                    String pr = new String(Files.readAllBytes(progressFile.toPath()), StandardCharsets.UTF_8);
                    if (!pr.equals(lastProgressRaw)) {
                        lastProgressRaw = pr;
                        Map<String, String> pm = DiskInstallPolicy.parseKeyValues(pr);
                        listener.onProgress(
                                pm.getOrDefault("stage", ""),
                                DiskInstallPolicy.parseIntOrZero(pm.get("percent")),
                                pm.getOrDefault("detail", ""));
                    }
                } catch (Exception ignored) {
                }
            }
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
                        throw new IOException(detail.isEmpty() ? (status.isEmpty() ? "install_failed" : status) : detail);
                    }
                } catch (IOException e) {
                    if (e.getMessage() != null && !e.getMessage().isEmpty()
                            && !e.getMessage().contains("No such file")) {
                        throw e;
                    }
                }
            }
            SystemClock.sleep(250);
        }
        throw new IOException("bridge_timeout");
    }

    private static String newNonce() {
        return UUID.randomUUID().toString().toLowerCase(Locale.US);
    }
}
