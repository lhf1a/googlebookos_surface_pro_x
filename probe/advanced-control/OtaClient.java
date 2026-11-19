package org.surface.advancedcontrol;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

/** File-based IPC client talking to the built-in initramfs OTA root daemon (/bin/spx-ota-boot). */
final class OtaClient {
    static final class OtaState {
        boolean bridgeReady;
        String currentVersion = "";
        String currentCommit = "";
        String currentKernel = "";
        String currentSuperSha256 = "";
        long superBytes;
        long espFreeMb;
        long espTotalMb;
        long dataFreeMb;
        boolean pendingActive;
        String pendingKind = "";
        String pendingVersion = "";
        boolean pendingWipeData;
        boolean pendingApplySuper;
        String lastStatus = "";
        String lastVersion = "";
        String lastKind = "";
        boolean lastWipeData;
        String lastError = "";
    }

    static final class PackageInfo {
        String displayName = "";
        String packageKind = "";
        String packageSha256 = "";
        long packageBytes;
        String version = "";
        String kernelRelease = "";
        String buildCommit = "";
        boolean superIncluded;
        String superSha256 = "";
        long superBytes;
        boolean superCompatible = true;
        String espMode = "";
        long espBytes;
        int fileCount;
    }

    interface ProgressListener {
        void onProgress(String stage, int percent, String detail);
    }

    private final File bridgeDir;

    OtaClient(Context context) {
        this.bridgeDir = context.createDeviceProtectedStorageContext().getFilesDir();
        if (!bridgeDir.isDirectory()) {
            bridgeDir.mkdirs();
        }
    }

    OtaState loadState(boolean requestRefresh) {
        boolean bridgeOk = false;
        if (requestRefresh) {
            try {
                sendRequest(OtaPolicy.refreshRequest(newNonce()), 5000, null);
                bridgeOk = true;
            } catch (Exception ignored) {
                bridgeOk = false;
            }
        }
        OtaState state = new OtaState();
        File stateFile = new File(bridgeDir, "ota-state");
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
        if (!state.bridgeReady) {
            state.currentKernel = System.getProperty("os.version", "");
        }
        return state;
    }

    PackageInfo stageAndInspect(Context ctx, Uri uri, ProgressListener listener) throws Exception {
        if (!bridgeDir.isDirectory() && !bridgeDir.mkdirs()) {
            throw new IOException("bridge_dir_unavailable");
        }
        String displayName = queryDisplayName(ctx.getContentResolver(), uri);
        long totalSize = querySize(ctx.getContentResolver(), uri);

        File uploadFile = new File(bridgeDir, OtaPolicy.STAGED_UPLOAD_NAME);
        File tmpFile = new File(bridgeDir, OtaPolicy.STAGED_UPLOAD_NAME + ".part");
        if (tmpFile.exists()) {
            tmpFile.delete();
        }
        if (listener != null) {
            listener.onProgress("copying", 0, displayName);
        }

        byte[] buf = new byte[1024 * 1024];
        long copied = 0;
        int lastPct = -1;
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IOException("cannot_open_file");
            }
            try (FileOutputStream out = new FileOutputStream(tmpFile)) {
                int n;
                while ((n = in.read(buf)) >= 0) {
                    if (n == 0) {
                        continue;
                    }
                    out.write(buf, 0, n);
                    copied += n;
                    if (listener != null) {
                        int pct = totalSize > 0
                                ? (int) Math.min(99L, (copied * 100L) / totalSize)
                                : -1;
                        if (pct != lastPct || pct < 0) {
                            lastPct = pct;
                            listener.onProgress("copying", pct, OtaPolicy.formatBytes(copied));
                        }
                    }
                }
                out.getFD().sync();
            }
        } catch (Exception e) {
            tmpFile.delete();
            throw e;
        }
        if (copied <= 0) {
            tmpFile.delete();
            throw new IOException("empty_package");
        }
        if (uploadFile.exists()) {
            uploadFile.delete();
        }
        if (!tmpFile.renameTo(uploadFile)) {
            tmpFile.delete();
            throw new IOException("stage_rename_failed");
        }

        if (listener != null) {
            listener.onProgress("hashing", 10, displayName);
        }
        String payload = sendRequest(
                OtaPolicy.inspectRequest(newNonce(), OtaPolicy.STAGED_UPLOAD_NAME),
                600000L,
                listener);
        PackageInfo info = parsePackageInfo(payload);
        info.displayName = displayName;
        return info;
    }

    String installPackage(String expectedSha256, boolean wipeData, ProgressListener listener) throws Exception {
        return sendRequest(
                OtaPolicy.installRequest(newNonce(), expectedSha256, wipeData),
                600000L,
                listener).trim();
    }

    OtaState cancelPending() throws Exception {
        sendRequest(OtaPolicy.cancelRequest(newNonce()), 15000L, null);
        return loadState(false);
    }

    OtaState clearResult() throws Exception {
        sendRequest(OtaPolicy.clearResultRequest(newNonce()), 10000L, null);
        return loadState(false);
    }

    void requestReboot() throws Exception {
        sendRequest(OtaPolicy.rebootRequest(newNonce()), 10000L, null);
    }

    private String sendRequest(String body, long timeoutMs, ProgressListener listener) throws Exception {
        if (!bridgeDir.isDirectory() && !bridgeDir.mkdirs()) {
            throw new IOException("bridge_dir_unavailable");
        }
        String[] lines = body.split("\n");
        if (lines.length < 2) {
            throw new IllegalArgumentException("Malformed OTA request");
        }
        String nonce = lines[1];
        File progressFile = new File(bridgeDir, "ota-progress");
        if (progressFile.isFile()) {
            progressFile.delete();
        }
        File tmp = new File(bridgeDir, "ota-request.tmp");
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            fos.write(body.getBytes(StandardCharsets.UTF_8));
            fos.getFD().sync();
        }
        File target = new File(bridgeDir, "ota-request");
        if (!tmp.renameTo(target)) {
            tmp.delete();
            throw new IOException("unable_to_submit_request");
        }

        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        File replyFile = new File(bridgeDir, "ota-reply");
        String lastProgressRaw = "";
        while (SystemClock.elapsedRealtime() < deadline) {
            if (listener != null && progressFile.isFile()) {
                try {
                    String pr = new String(Files.readAllBytes(progressFile.toPath()), StandardCharsets.UTF_8);
                    if (!pr.equals(lastProgressRaw)) {
                        lastProgressRaw = pr;
                        Map<String, String> pm = OtaPolicy.parseKeyValues(pr);
                        listener.onProgress(
                                pm.getOrDefault("stage", ""),
                                OtaPolicy.parseIntOrZero(pm.get("percent")),
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
                        throw new IOException(detail.isEmpty() ? (status.isEmpty() ? "ota_error" : status) : detail);
                    }
                } catch (IOException e) {
                    if (e.getMessage() != null && !e.getMessage().isEmpty()
                            && !e.getMessage().contains("No such file")) {
                        throw e;
                    }
                }
            }
            SystemClock.sleep(200);
        }
        throw new IOException("bridge_timeout");
    }

    private static void parseStateInto(String raw, OtaState state) {
        Map<String, String> map = OtaPolicy.parseKeyValues(raw);
        if ("1".equals(map.get("ota_bridge_version"))) {
            state.bridgeReady = true;
        }
        state.currentVersion = map.getOrDefault("current_version", "");
        state.currentCommit = map.getOrDefault("current_commit", "");
        state.currentKernel = map.getOrDefault("current_kernel", "");
        state.currentSuperSha256 = map.getOrDefault("current_super_sha256", "");
        state.superBytes = OtaPolicy.parseLongOrZero(map.get("super_bytes"));
        state.espFreeMb = OtaPolicy.parseLongOrZero(map.get("esp_free_mb"));
        state.espTotalMb = OtaPolicy.parseLongOrZero(map.get("esp_total_mb"));
        state.dataFreeMb = OtaPolicy.parseLongOrZero(map.get("data_free_mb"));
        state.pendingActive = "1".equals(map.get("pending_active"));
        state.pendingKind = map.getOrDefault("pending_kind", "");
        state.pendingVersion = map.getOrDefault("pending_version", "");
        state.pendingWipeData = "1".equals(map.get("pending_wipe_data"));
        state.pendingApplySuper = "1".equals(map.get("pending_apply_super"));
        state.lastStatus = map.getOrDefault("last_status", "");
        state.lastVersion = map.getOrDefault("last_version", "");
        state.lastKind = map.getOrDefault("last_kind", "");
        state.lastWipeData = "1".equals(map.get("last_wipe_data"));
        state.lastError = map.getOrDefault("last_error", "");
    }

    private static PackageInfo parsePackageInfo(String raw) {
        Map<String, String> map = OtaPolicy.parseKeyValues(raw);
        PackageInfo info = new PackageInfo();
        info.packageKind = map.getOrDefault("package_kind", "");
        info.packageSha256 = map.getOrDefault("package_sha256", "");
        info.packageBytes = OtaPolicy.parseLongOrZero(map.get("package_bytes"));
        info.version = map.getOrDefault("version", "");
        info.kernelRelease = map.getOrDefault("kernel_release", "");
        info.buildCommit = map.getOrDefault("build_commit", "");
        info.superIncluded = "1".equals(map.get("super_included"));
        info.superSha256 = map.getOrDefault("super_sha256", "");
        info.superBytes = OtaPolicy.parseLongOrZero(map.get("super_bytes"));
        info.superCompatible = !"0".equals(map.get("super_compatible"));
        info.espMode = map.getOrDefault("esp_mode", "");
        info.espBytes = OtaPolicy.parseLongOrZero(map.get("esp_bytes"));
        info.fileCount = OtaPolicy.parseIntOrZero(map.get("file_count"));
        return info;
    }

    private static String queryDisplayName(ContentResolver cr, Uri uri) {
        try (Cursor c = cr.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String name = c.getString(0);
                if (name != null && !name.trim().isEmpty()) {
                    return name.trim();
                }
            }
        } catch (Exception ignored) {
        }
        String last = uri.getLastPathSegment();
        return (last != null && !last.isEmpty()) ? last : "update-package";
    }

    private static long querySize(ContentResolver cr, Uri uri) {
        try (Cursor c = cr.query(uri, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) {
                return c.getLong(0);
            }
        } catch (Exception ignored) {
        }
        return -1L;
    }

    private static String newNonce() {
        return UUID.randomUUID().toString().toLowerCase(Locale.US);
    }
}
