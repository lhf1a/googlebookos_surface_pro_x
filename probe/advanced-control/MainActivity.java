package org.surface.advancedcontrol;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Modern Android Settings-styled Advanced Settings app:
 *   Level 1: Home selection page (Vulkan Configuration / Performance Mode / Touch)
 *   Level 2a: Vulkan Configuration (Default MESA/Qcom/Auto + per-app list with modified apps pinned first)
 *   Level 2b: Performance Mode (Kernel-supported CPU scheduling policies)
 *   Level 2c: Touch (Ghost-touch patch toggle, calibration summary, recalibrate, import & clear)
 * Bilingual CN/EN following system language (non-Chinese defaults to English).
 */
public final class MainActivity extends Activity {
    private static final int PAGE_HOME = 0;
    private static final int PAGE_VULKAN = 1;
    private static final int PAGE_PERFORMANCE = 2;
    private static final int PAGE_TOUCH = 3;
    private static final int REQ_IMPORT_TOUCH = 401;
    private static final int REQ_EXPORT_TOUCH = 402;
    private static final String STATE_PAGE = "page";
    private static final String STATE_QUERY = "query";
    private static final String STATE_EXPORT_CONTENT = "export_content";
    private static final String CALIBRATION_PKG = "org.surface.touchcalibration.unified";
    private static final String CALIBRATION_ACTIVITY = "org.surface.touchcalibration.MainActivity";

    private static final String[] DRIVER_VALUES = {"", "turnip", "qcom"};

    private static final class AppItem {
        final ApplicationInfo info;
        final String label;
        final String pkg;
        final String propKey;
        final Drawable icon;

        AppItem(ApplicationInfo info, String label, String propKey, Drawable icon) {
            this.info = info;
            this.label = label;
            this.pkg = info.packageName;
            this.propKey = propKey;
            this.icon = icon;
        }
    }

    private static final class ListEntry {
        final boolean isHeader;
        final String headerTitle;
        final AppItem app;
        final boolean firstInGroup;
        final boolean lastInGroup;

        private ListEntry(String headerTitle) {
            this.isHeader = true;
            this.headerTitle = headerTitle;
            this.app = null;
            this.firstInGroup = false;
            this.lastInGroup = false;
        }

        private ListEntry(AppItem app, boolean firstInGroup, boolean lastInGroup) {
            this.isHeader = false;
            this.headerTitle = null;
            this.app = app;
            this.firstInGroup = firstInGroup;
            this.lastInGroup = lastInGroup;
        }
    }

    private SettingsTheme theme;
    private Strings str;
    private BridgeClient bridge;

    private int currentPage = PAGE_HOME;
    private String searchQuery = "";
    private boolean busy = false;
    private boolean loadedOnce = false;
    private boolean pendingCalibrationRefresh = false;
    private String pendingExportContent = "";
    private String bannerText = "";
    private boolean bannerIsError = false;

    private int currentUser = 0;
    private BridgeClient.BridgeState bridgeState = new BridgeClient.BridgeState();
    private final List<AppItem> allApps = new ArrayList<>();
    private final Map<String, String> savedAppDrivers = new HashMap<>();
    private final List<ListEntry> vulkanEntries = new ArrayList<>();

    private FrameLayout rootFrame;
    private FrameLayout contentSlot;
    private ListView vulkanListView;
    private VulkanListAdapter vulkanAdapter;
    private TextView vulkanSummaryView;
    private TextView statusBannerView;
    private EditText searchInput;
    private View searchClearBtn;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            currentPage = savedInstanceState.getInt(STATE_PAGE, PAGE_HOME);
            searchQuery = savedInstanceState.getString(STATE_QUERY, "");
            pendingExportContent = savedInstanceState.getString(STATE_EXPORT_CONTENT, "");
        }

        Locale locale = getResources().getConfiguration().getLocales().get(0);
        str = new Strings(locale);
        theme = new SettingsTheme(this);
        theme.styleSystemBars(this);
        bridge = new BridgeClient(this);
        currentUser = android.os.Process.myUid() / 100000;

        rootFrame = new FrameLayout(this);
        rootFrame.setBackgroundColor(theme.bg);
        contentSlot = new FrameLayout(this);
        int maxColWidth = Math.min(getResources().getDisplayMetrics().widthPixels, theme.dp(760));
        FrameLayout.LayoutParams slotLp = new FrameLayout.LayoutParams(
                maxColWidth, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.CENTER_HORIZONTAL);
        rootFrame.addView(contentSlot, slotLp);

        rootFrame.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                v.setPadding(
                        insets.getSystemWindowInsetLeft(),
                        insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom());
            }
            return insets;
        });

        setContentView(rootFrame);
        renderCurrentPage();
        reloadAll(true);
    }

    @Override protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putInt(STATE_PAGE, currentPage);
        outState.putString(STATE_QUERY, searchQuery);
        outState.putString(STATE_EXPORT_CONTENT, pendingExportContent);
    }

    @Override protected void onResume() {
        super.onResume();
        if (pendingCalibrationRefresh && !busy) {
            pendingCalibrationRefresh = false;
            reloadAll(true);
        }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_EXPORT_TOUCH) {
            String content = pendingExportContent;
            pendingExportContent = "";
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                return;
            }
            if (content.isEmpty()) {
                bannerText = str.touchExportFailed("empty_export");
                bannerIsError = true;
                renderCurrentPage();
                return;
            }
            try {
                writeExportText(data.getData(), content);
                bannerText = str.touchExportSuccess();
                bannerIsError = false;
            } catch (Exception e) {
                bannerText = str.touchExportFailed(
                        e.getMessage() != null ? e.getMessage() : e.toString());
                bannerIsError = true;
            }
            renderCurrentPage();
            return;
        }
        if (requestCode != REQ_IMPORT_TOUCH || resultCode != RESULT_OK
                || data == null || data.getData() == null) {
            return;
        }
        try {
            String rawContent = readImportText(data.getData());
            DriverPolicy.detectTouchImportKind(rawContent);
            importTouchCalibration(rawContent);
        } catch (Exception e) {
            bannerText = str.touchImportInvalid(
                    e.getMessage() != null ? e.getMessage() : e.toString());
            bannerIsError = true;
            renderCurrentPage();
        }
    }

    private void writeExportText(Uri uri, String content) throws Exception {
        try (OutputStream out = getContentResolver().openOutputStream(uri, "w")) {
            if (out == null) {
                throw new IllegalArgumentException("unwritable_file");
            }
            out.write(content.getBytes(StandardCharsets.UTF_8));
            out.flush();
        }
    }

    private String readImportText(Uri uri) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IllegalArgumentException("unreadable_file");
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int total = 0;
            int read;
            while ((read = in.read(buf)) >= 0) {
                total += read;
                if (total > DriverPolicy.MAX_TOUCH_IMPORT_BYTES) {
                    throw new IllegalArgumentException("file_too_large");
                }
                out.write(buf, 0, read);
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    @Override public void onBackPressed() {
        if (currentPage != PAGE_HOME) {
            navigateTo(PAGE_HOME);
            return;
        }
        super.onBackPressed();
    }

    private void navigateTo(int page) {
        currentPage = page;
        bannerText = "";
        bannerIsError = false;
        renderCurrentPage();
    }

    private int countCustomizedApps() {
        int count = 0;
        for (AppItem app : allApps) {
            if (DriverPolicy.isCustomized(savedAppDrivers.get(app.propKey))) {
                count++;
            }
        }
        return count;
    }

    // ---- Async Bridge & System Data Loading ----

    private void reloadAll(boolean requestBridgeRefresh) {
        if (busy) {
            return;
        }
        busy = true;
        bannerText = str.loadingStatus();
        bannerIsError = false;
        renderCurrentPage();

        new Thread(() -> {
            try {
                BridgeClient.BridgeState nextState = bridge.loadState(requestBridgeRefresh);
                Map<String, String> nextSaved = bridge.readSavedAppDrivers();

                PackageManager pm = getPackageManager();
                List<AppItem> loadedApps = new ArrayList<>();
                int user = android.os.Process.myUid() / 100000;
                for (ApplicationInfo app : pm.getInstalledApplications(0)) {
                    if (app.uid / 100000 != user || app.packageName.equals(getPackageName())) {
                        continue;
                    }
                    String key;
                    try {
                        key = DriverPolicy.propertyKey(app.uid, app.packageName);
                    } catch (IllegalArgumentException e) {
                        continue;
                    }
                    String label = app.loadLabel(pm).toString();
                    Drawable icon = null;
                    try {
                        icon = app.loadIcon(pm);
                    } catch (Exception ignored) {
                    }
                    loadedApps.add(new AppItem(app, label, key, icon));
                }

                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    currentUser = user;
                    bridgeState = nextState;
                    savedAppDrivers.clear();
                    savedAppDrivers.putAll(nextSaved);
                    allApps.clear();
                    allApps.addAll(loadedApps);
                    loadedOnce = true;
                    busy = false;
                    bannerText = "";
                    bannerIsError = false;
                    renderCurrentPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.readFailed(e.getMessage() != null ? e.getMessage() : e.toString());
                    bannerIsError = true;
                    renderCurrentPage();
                });
            }
        }, "advanced-control-load").start();
    }

    private void saveDefaultVulkan(String driver) {
        if (busy || !bridgeState.bridgeReady || !bridgeState.vulkanAvailable) {
            return;
        }
        if (driver.equals(bridgeState.vulkanDefault)) {
            return;
        }
        busy = true;
        bannerText = str.savingStatus();
        bannerIsError = false;
        renderCurrentPage();

        new Thread(() -> {
            try {
                BridgeClient.BridgeState nextState = bridge.setDefaultVulkan(driver);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    bridgeState = nextState;
                    busy = false;
                    bannerText = str.savedDefaultMessage(driver);
                    bannerIsError = false;
                    renderCurrentPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.saveFailed(e.getMessage() != null ? e.getMessage() : e.toString());
                    bannerIsError = true;
                    renderCurrentPage();
                });
            }
        }, "advanced-control-save-default").start();
    }

    private void saveAppVulkan(AppItem app, String driver, boolean forceStop) {
        if (busy || !bridgeState.bridgeReady || !bridgeState.vulkanAvailable) {
            return;
        }
        busy = true;
        bannerText = str.savingStatus();
        bannerIsError = false;
        updateVulkanBannerOnly();

        new Thread(() -> {
            try {
                BridgeClient.BridgeState nextState = bridge.setAppVulkan(app.info.uid, app.pkg, driver, forceStop);
                Map<String, String> nextSaved = bridge.readSavedAppDrivers();
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    bridgeState = nextState;
                    savedAppDrivers.clear();
                    savedAppDrivers.putAll(nextSaved);
                    busy = false;
                    bannerText = str.savedAppMessage(forceStop);
                    bannerIsError = false;
                    rebuildVulkanEntries();
                    if (vulkanAdapter != null) {
                        vulkanAdapter.notifyDataSetChanged();
                    }
                    updateVulkanBannerOnly();
                    if (vulkanSummaryView != null) {
                        vulkanSummaryView.setText(str.vulkanSummaryBar(
                                currentUser, allApps.size(), countCustomizedApps(), bridgeState.vulkanAvailable));
                    }
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.saveFailed(e.getMessage() != null ? e.getMessage() : e.toString());
                    bannerIsError = true;
                    updateVulkanBannerOnly();
                });
            }
        }, "advanced-control-save-app").start();
    }

    private void saveCpuGovernor(String governor) {
        if (busy || !bridgeState.bridgeReady) {
            return;
        }
        busy = true;
        bannerText = str.savingStatus();
        bannerIsError = false;
        renderCurrentPage();

        new Thread(() -> {
            try {
                BridgeClient.BridgeState nextState = bridge.setCpuGovernor(governor);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    bridgeState = nextState;
                    busy = false;
                    bannerText = str.perfSavedMessage(governor);
                    bannerIsError = false;
                    renderCurrentPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.saveFailed(e.getMessage() != null ? e.getMessage() : e.toString());
                    bannerIsError = true;
                    renderCurrentPage();
                });
            }
        }, "advanced-control-save-gov").start();
    }

    @FunctionalInterface
    private interface TouchBridgeCall {
        String run() throws Exception;
    }

    private void runTouchBridgeCall(String threadName, boolean isImport, TouchBridgeCall call) {
        if (busy || !bridgeState.bridgeReady) {
            return;
        }
        busy = true;
        bannerText = str.savingStatus();
        bannerIsError = false;
        renderCurrentPage();

        new Thread(() -> {
            try {
                String msg = call.run();
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = msg;
                    bannerIsError = false;
                    renderCurrentPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    String detail = e.getMessage() != null ? e.getMessage() : e.toString();
                    bannerText = isImport ? str.touchImportInvalid(detail) : str.saveFailed(detail);
                    bannerIsError = true;
                    renderCurrentPage();
                });
            }
        }, threadName).start();
    }

    private void saveTouchGhostPatch(boolean enabled) {
        runTouchBridgeCall("advanced-control-touch-ghost", false, () -> {
            bridgeState = bridge.setTouchGhostPatch(enabled);
            return str.touchGhostSavedMessage(enabled);
        });
    }

    private void clearTouchCalibration() {
        runTouchBridgeCall("advanced-control-touch-clear", false, () -> {
            bridgeState = bridge.clearTouchCalibration();
            return str.touchClearedMessage();
        });
    }

    private void importTouchCalibration(String rawContent) {
        runTouchBridgeCall("advanced-control-touch-import", true, () -> {
            BridgeClient.TouchImportResult result = bridge.importTouchCalibration(rawContent);
            bridgeState = result.state;
            return str.touchImportSuccess(result.kind);
        });
    }

    private void openCalibrationApp() {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setClassName(CALIBRATION_PKG, CALIBRATION_ACTIVITY);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            pendingCalibrationRefresh = true;
            startActivity(intent);
        } catch (ActivityNotFoundException e) {
            pendingCalibrationRefresh = false;
            bannerText = str.touchCalibrationAppMissing();
            bannerIsError = true;
            renderCurrentPage();
        }
    }

    private void openTouchImportPicker() {
        if (busy || !bridgeState.bridgeReady || !bridgeState.touchAvailable) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQ_IMPORT_TOUCH);
        } catch (ActivityNotFoundException e) {
            bannerText = str.touchImportInvalid(e.getMessage() != null ? e.getMessage() : "picker_unavailable");
            bannerIsError = true;
            renderCurrentPage();
        }
    }

    private void prepareTouchExport() {
        if (busy || !bridgeState.bridgeReady || !bridgeState.touchAvailable) {
            return;
        }
        busy = true;
        bannerText = "";
        bannerIsError = false;
        renderCurrentPage();

        new Thread(() -> {
            try {
                String content = bridge.exportTouchCalibration();
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    pendingExportContent = content;
                    launchTouchExportPicker();
                    renderCurrentPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.touchExportFailed(e.getMessage() != null ? e.getMessage() : e.toString());
                    bannerIsError = true;
                    renderCurrentPage();
                });
            }
        }, "advanced-control-touch-export").start();
    }

    private void launchTouchExportPicker() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_TITLE,
                DriverPolicy.defaultTouchExportFilename(bridgeState.touchDeviceId));
        try {
            startActivityForResult(intent, REQ_EXPORT_TOUCH);
        } catch (ActivityNotFoundException e) {
            pendingExportContent = "";
            bannerText = str.touchExportFailed(e.getMessage() != null ? e.getMessage() : "picker_unavailable");
            bannerIsError = true;
        }
    }

    private void confirmClearTouchCalibration() {
        if (busy || !bridgeState.bridgeReady || !bridgeState.touchAvailable) {
            return;
        }
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(str.touchClearDialogTitle())
                .setMessage(str.touchClearDialogMessage())
                .setNegativeButton(str.cancel(), null)
                .setPositiveButton(str.touchClearConfirm(), (d, w) -> clearTouchCalibration())
                .show();
    }

    // ---- Page Rendering ----

    private void renderCurrentPage() {
        contentSlot.removeAllViews();
        vulkanListView = null;
        vulkanAdapter = null;
        vulkanSummaryView = null;
        statusBannerView = null;
        searchInput = null;
        searchClearBtn = null;

        if (currentPage == PAGE_VULKAN) {
            contentSlot.addView(buildVulkanPage(), new FrameLayout.LayoutParams(-1, -1));
        } else if (currentPage == PAGE_PERFORMANCE) {
            contentSlot.addView(buildPerformancePage(), new FrameLayout.LayoutParams(-1, -1));
        } else if (currentPage == PAGE_TOUCH) {
            contentSlot.addView(buildTouchPage(), new FrameLayout.LayoutParams(-1, -1));
        } else {
            contentSlot.addView(buildHomePage(), new FrameLayout.LayoutParams(-1, -1));
        }
    }

    // ---- Level 1: Modern Android Settings Home Page ----

    private View buildHomePage() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS);

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        int hPad = theme.dp(20);
        container.setPadding(hPad, theme.dp(16), hPad, theme.dp(28));

        // Top action row with Refresh button
        LinearLayout topRow = new LinearLayout(this);
        topRow.setOrientation(LinearLayout.HORIZONTAL);
        topRow.setGravity(Gravity.CENTER_VERTICAL);
        View spacer = new View(this);
        topRow.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1f));
        topRow.addView(buildIconButton(SettingsTheme.ICON_REFRESH, str.refreshDesc(), () -> reloadAll(true)));
        container.addView(topRow);

        // Expressive Android Settings large title
        TextView title = new TextView(this);
        title.setText(str.appTitle());
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(-1, -2);
        titleLp.topMargin = theme.dp(4);
        container.addView(title, titleLp);

        TextView subtitle = new TextView(this);
        subtitle.setText(str.homeSubtitle());
        subtitle.setTextColor(theme.textSecondary);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(-1, -2);
        subLp.topMargin = theme.dp(4);
        subLp.bottomMargin = theme.dp(20);
        container.addView(subtitle, subLp);

        // Optional status / bridge warning banner
        if (!bridgeState.bridgeReady && !busy) {
            container.addView(buildBanner(str.bridgeMissingBanner(), true), marginBottom(16));
        } else if (!bannerText.isEmpty()) {
            container.addView(buildBanner(bannerText, bannerIsError), marginBottom(16));
        }

        // Grouped Material 3 Settings Card: Vulkan Config + Performance Mode
        LinearLayout cardGroup = new LinearLayout(this);
        cardGroup.setOrientation(LinearLayout.VERTICAL);

        String defaultLabel = str.driverDisplayName(bridgeState.vulkanDefault);
        String vkSummary = !loadedOnce && busy
                ? str.loadingSummary()
                : str.vulkanSubtitle(defaultLabel, countCustomizedApps(), bridgeState.vulkanAvailable);
        View vulkanRow = buildCategoryRow(
                SettingsTheme.ICON_VULKAN,
                theme.vulkanBadgeBg,
                theme.vulkanBadgeFg,
                str.vulkanTitle(),
                vkSummary,
                24f,
                4f,
                () -> navigateTo(PAGE_VULKAN));
        cardGroup.addView(vulkanRow);

        View gap = new View(this);
        cardGroup.addView(gap, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        boolean perfAvail = !bridgeState.availableGovernors.isEmpty();
        String perfSummary = !loadedOnce && busy
                ? str.loadingSummary()
                : str.perfSubtitle(bridgeState.currentGovernor, perfAvail);
        View perfRow = buildCategoryRow(
                SettingsTheme.ICON_PERF,
                theme.perfBadgeBg,
                theme.perfBadgeFg,
                str.perfTitle(),
                perfSummary,
                4f,
                4f,
                () -> navigateTo(PAGE_PERFORMANCE));
        cardGroup.addView(perfRow);

        View gap2 = new View(this);
        cardGroup.addView(gap2, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        String touchSummary = !loadedOnce && busy
                ? str.loadingSummary()
                : str.touchHomeSubtitle(
                        bridgeState.touchAvailable,
                        bridgeState.touchNoiseSaved,
                        bridgeState.touchNoiseEnabled,
                        bridgeState.touchProfileSaved);
        View touchRow = buildCategoryRow(
                SettingsTheme.ICON_TOUCH,
                theme.touchBadgeBg,
                theme.touchBadgeFg,
                str.touchTitle(),
                touchSummary,
                4f,
                4f,
                () -> navigateTo(PAGE_TOUCH));
        cardGroup.addView(touchRow);

        View gap3 = new View(this);
        cardGroup.addView(gap3, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        View otaRow = buildCategoryRow(
                SettingsTheme.ICON_UPDATE,
                theme.otaBadgeBg,
                theme.otaBadgeFg,
                str.otaTitle(),
                str.otaHomeSubtitle(),
                4f,
                24f,
                () -> startActivity(new Intent(this, SystemUpdateActivity.class)));
        cardGroup.addView(otaRow);

        container.addView(cardGroup, marginBottom(20));

        // Subtle footer info card
        TextView footer = new TextView(this);
        footer.setText(str.homeFooter());
        footer.setTextColor(theme.textSecondary);
        footer.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        footer.setLineSpacing(theme.dp(3), 1.0f);
        footer.setBackground(theme.roundedRect(theme.cardAlt, 18));
        footer.setPadding(theme.dp(16), theme.dp(14), theme.dp(16), theme.dp(14));
        container.addView(footer);

        scroll.addView(container);
        return scroll;
    }

    private View buildCategoryRow(int iconKind, int badgeBg, int badgeFg,
                                  String titleText, String summaryText,
                                  float topRadiusDp, float bottomRadiusDp,
                                  Runnable onClick) {
        return buildCategoryRow(
                iconKind, badgeBg, badgeFg, titleText, summaryText,
                topRadiusDp, bottomRadiusDp, true, onClick);
    }

    private View buildCategoryRow(int iconKind, int badgeBg, int badgeFg,
                                  String titleText, String summaryText,
                                  float topRadiusDp, float bottomRadiusDp,
                                  boolean enabled, Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setBackground(theme.cardItemBackground(topRadiusDp, bottomRadiusDp, theme.card));
        row.setPadding(theme.dp(16), theme.dp(18), theme.dp(16), theme.dp(18));
        row.setEnabled(enabled);
        row.setClickable(enabled);
        row.setFocusable(enabled);
        if (!enabled) {
            row.setAlpha(0.55f);
        } else {
            row.setOnClickListener(v -> onClick.run());
        }

        FrameLayout badge = new FrameLayout(this);
        badge.setBackground(theme.roundedRect(badgeBg, 999));
        View glyph = theme.iconView(this, iconKind, badgeFg, 24);
        badge.addView(glyph, new FrameLayout.LayoutParams(theme.dp(24), theme.dp(24), Gravity.CENTER));
        LinearLayout.LayoutParams badgeLp = new LinearLayout.LayoutParams(theme.dp(44), theme.dp(44));
        badgeLp.rightMargin = theme.dp(16);
        row.addView(badge, badgeLp);

        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);

        TextView title = new TextView(this);
        title.setText(titleText);
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        texts.addView(title);

        TextView summary = new TextView(this);
        summary.setText(summaryText);
        summary.setTextColor(theme.textSecondary);
        summary.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        LinearLayout.LayoutParams sumLp = new LinearLayout.LayoutParams(-1, -2);
        sumLp.topMargin = theme.dp(3);
        texts.addView(summary, sumLp);

        row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1f));

        View chevron = theme.iconView(this, SettingsTheme.ICON_CHEVRON, theme.textSecondary, 20);
        LinearLayout.LayoutParams chevLp = new LinearLayout.LayoutParams(theme.dp(20), theme.dp(20));
        chevLp.leftMargin = theme.dp(8);
        row.addView(chevron, chevLp);

        return row;
    }

    // ---- Level 2a: Vulkan Configuration Page ----

    private View buildVulkanPage() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int hPad = theme.dp(18);
        root.setPadding(hPad, theme.dp(8), hPad, theme.dp(12));

        root.addView(buildSubPageHeader(str.vulkanTitle(), () -> navigateTo(PAGE_HOME), () -> reloadAll(true)));

        // Status banner slot
        statusBannerView = buildBanner("", false);
        root.addView(statusBannerView, marginBottom(10));
        updateVulkanBannerOnly();

        // Default Vulkan Configuration Card (Auto / MESA / Qcom)
        LinearLayout defaultCard = new LinearLayout(this);
        defaultCard.setOrientation(LinearLayout.VERTICAL);
        defaultCard.setBackground(theme.roundedRect(theme.card, 22));
        defaultCard.setPadding(theme.dp(16), theme.dp(14), theme.dp(16), theme.dp(14));

        LinearLayout defHeader = new LinearLayout(this);
        defHeader.setOrientation(LinearLayout.HORIZONTAL);
        defHeader.setGravity(Gravity.CENTER_VERTICAL);

        TextView defTitle = new TextView(this);
        defTitle.setText(str.defaultConfigSection());
        defTitle.setTextColor(theme.textPrimary);
        defTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
        defTitle.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        defHeader.addView(defTitle, new LinearLayout.LayoutParams(0, -2, 1f));

        defHeader.addView(buildDriverChip(bridgeState.vulkanDefault, true));
        defaultCard.addView(defHeader);

        // 3-segment Material 3 pill toggle: Auto | MESA | Qcom
        LinearLayout segWrap = new LinearLayout(this);
        segWrap.setOrientation(LinearLayout.HORIZONTAL);
        segWrap.setBackground(theme.roundedRect(theme.cardAlt, 999));
        segWrap.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(4));
        LinearLayout.LayoutParams segLp = new LinearLayout.LayoutParams(-1, -2);
        segLp.topMargin = theme.dp(12);
        segLp.bottomMargin = theme.dp(10);

        boolean canEditDefault = !busy && bridgeState.bridgeReady && bridgeState.vulkanAvailable;
        segWrap.addView(buildSegmentButton(str.driverAutoLabel(), "".equals(bridgeState.vulkanDefault),
                canEditDefault, () -> saveDefaultVulkan("")), new LinearLayout.LayoutParams(0, -2, 1f));
        segWrap.addView(buildSegmentButton(str.driverMesaLabel(), "turnip".equals(bridgeState.vulkanDefault),
                canEditDefault, () -> saveDefaultVulkan("turnip")), new LinearLayout.LayoutParams(0, -2, 1f));
        segWrap.addView(buildSegmentButton(str.driverQcomLabel(), "qcom".equals(bridgeState.vulkanDefault),
                canEditDefault, () -> saveDefaultVulkan("qcom")), new LinearLayout.LayoutParams(0, -2, 1f));
        defaultCard.addView(segWrap, segLp);

        TextView defHint = new TextView(this);
        defHint.setText(str.defaultConfigHint(bridgeState.vulkanDefault, bridgeState.policyDefault));
        defHint.setTextColor(theme.textSecondary);
        defHint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        defHint.setLineSpacing(theme.dp(2), 1.0f);
        defaultCard.addView(defHint);

        root.addView(defaultCard, marginBottom(12));

        // Modern pill Search Bar
        LinearLayout searchBar = new LinearLayout(this);
        searchBar.setOrientation(LinearLayout.HORIZONTAL);
        searchBar.setGravity(Gravity.CENTER_VERTICAL);
        searchBar.setBackground(theme.roundedRectStroke(theme.searchBg, theme.divider, 1f, 999));
        searchBar.setPadding(theme.dp(14), theme.dp(4), theme.dp(8), theme.dp(4));

        View searchIcon = theme.iconView(this, SettingsTheme.ICON_SEARCH, theme.textSecondary, 20);
        LinearLayout.LayoutParams siLp = new LinearLayout.LayoutParams(theme.dp(20), theme.dp(20));
        siLp.rightMargin = theme.dp(10);
        searchBar.addView(searchIcon, siLp);

        searchInput = new EditText(this);
        searchInput.setSingleLine(true);
        searchInput.setBackground(null);
        searchInput.setHint(str.searchHint());
        searchInput.setHintTextColor(theme.textSecondary);
        searchInput.setTextColor(theme.textPrimary);
        searchInput.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        searchInput.setText(searchQuery);
        searchInput.setSelection(searchQuery.length());
        searchInput.setPadding(0, theme.dp(8), 0, theme.dp(8));
        searchBar.addView(searchInput, new LinearLayout.LayoutParams(0, -2, 1f));

        searchClearBtn = buildIconButton(SettingsTheme.ICON_CLEAR, str.clearDesc(), () -> {
            if (searchInput != null) {
                searchInput.setText("");
            }
        });
        searchClearBtn.setVisibility(searchQuery.isEmpty() ? View.GONE : View.VISIBLE);
        searchBar.addView(searchClearBtn);

        root.addView(searchBar, marginBottom(8));

        // Summary line
        vulkanSummaryView = new TextView(this);
        vulkanSummaryView.setText(str.vulkanSummaryBar(
                currentUser, allApps.size(), countCustomizedApps(), bridgeState.vulkanAvailable));
        vulkanSummaryView.setTextColor(theme.textSecondary);
        vulkanSummaryView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        vulkanSummaryView.setPadding(theme.dp(6), theme.dp(2), theme.dp(6), theme.dp(6));
        root.addView(vulkanSummaryView);

        // App list (customized apps pinned at the very top!)
        rebuildVulkanEntries();
        vulkanListView = new ListView(this);
        vulkanListView.setDivider(null);
        vulkanListView.setDividerHeight(0);
        vulkanListView.setSelector(android.R.color.transparent);
        vulkanListView.setClipToPadding(false);
        vulkanListView.setPadding(0, 0, 0, theme.dp(16));
        vulkanAdapter = new VulkanListAdapter();
        vulkanListView.setAdapter(vulkanAdapter);
        vulkanListView.setOnItemClickListener((parent, view, pos, id) -> {
            if (pos < 0 || pos >= vulkanEntries.size()) {
                return;
            }
            ListEntry entry = vulkanEntries.get(pos);
            if (!entry.isHeader && entry.app != null && !busy
                    && bridgeState.bridgeReady && bridgeState.vulkanAvailable) {
                showAppDriverDialog(entry.app);
            }
        });
        root.addView(vulkanListView, new LinearLayout.LayoutParams(-1, 0, 1f));

        searchInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                searchQuery = s == null ? "" : s.toString();
                if (searchClearBtn != null) {
                    searchClearBtn.setVisibility(searchQuery.isEmpty() ? View.GONE : View.VISIBLE);
                }
                rebuildVulkanEntries();
                if (vulkanAdapter != null) {
                    vulkanAdapter.notifyDataSetChanged();
                }
            }
            @Override public void afterTextChanged(Editable s) {}
        });

        return root;
    }

    private void updateVulkanBannerOnly() {
        if (statusBannerView == null) {
            return;
        }
        String text = !bridgeState.bridgeReady && !busy ? str.bridgeMissingBanner() : bannerText;
        boolean err = (!bridgeState.bridgeReady && !busy) || bannerIsError;
        if (text == null || text.isEmpty()) {
            statusBannerView.setVisibility(View.GONE);
        } else {
            statusBannerView.setVisibility(View.VISIBLE);
            statusBannerView.setText(text);
            statusBannerView.setTextColor(err ? theme.warnFg : theme.okFg);
            statusBannerView.setBackground(theme.roundedRect(err ? theme.warnBg : theme.okBg, 14));
        }
    }

    private void rebuildVulkanEntries() {
        vulkanEntries.clear();
        List<AppItem> customized = new ArrayList<>();
        List<AppItem> defaults = new ArrayList<>();
        for (AppItem app : allApps) {
            if (!DriverPolicy.matchesQuery(app.label, app.pkg, searchQuery)) {
                continue;
            }
            if (DriverPolicy.isCustomized(savedAppDrivers.get(app.propKey))) {
                customized.add(app);
            } else {
                defaults.add(app);
            }
        }
        Collections.sort(customized, (a, b) -> DriverPolicy.compareAppRows(true, a.label, a.pkg, true, b.label, b.pkg));
        Collections.sort(defaults, (a, b) -> DriverPolicy.compareAppRows(false, a.label, a.pkg, false, b.label, b.pkg));

        if (!customized.isEmpty()) {
            vulkanEntries.add(new ListEntry(str.customSectionHeader(customized.size())));
            for (int i = 0; i < customized.size(); i++) {
                vulkanEntries.add(new ListEntry(customized.get(i), i == 0, i == customized.size() - 1));
            }
        }
        if (!defaults.isEmpty()) {
            vulkanEntries.add(new ListEntry(str.defaultSectionHeader(defaults.size())));
            for (int i = 0; i < defaults.size(); i++) {
                vulkanEntries.add(new ListEntry(defaults.get(i), i == 0, i == defaults.size() - 1));
            }
        }
        if (vulkanEntries.isEmpty() && !busy) {
            vulkanEntries.add(new ListEntry(str.emptySearchResult()));
        }
    }

    private final class VulkanListAdapter extends BaseAdapter {
        @Override public int getCount() { return vulkanEntries.size(); }
        @Override public Object getItem(int position) { return vulkanEntries.get(position); }
        @Override public long getItemId(int position) { return position; }
        @Override public boolean areAllItemsEnabled() { return false; }
        @Override public boolean isEnabled(int position) {
            return position >= 0 && position < vulkanEntries.size() && !vulkanEntries.get(position).isHeader;
        }

        @Override public View getView(int position, View convertView, ViewGroup parent) {
            ListEntry entry = vulkanEntries.get(position);
            if (entry.isHeader) {
                TextView header = new TextView(MainActivity.this);
                header.setText(entry.headerTitle);
                header.setTextColor(theme.primary);
                header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
                header.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
                header.setPadding(theme.dp(8), theme.dp(12), theme.dp(8), theme.dp(6));
                return header;
            }

            AppItem app = entry.app;
            String savedVal = savedAppDrivers.get(app.propKey);
            boolean isCustom = DriverPolicy.isCustomized(savedVal);

            FrameLayout wrapper = new FrameLayout(MainActivity.this);
            wrapper.setPadding(0, entry.firstInGroup ? 0 : theme.dp(2), 0, entry.lastInGroup ? theme.dp(4) : 0);

            LinearLayout row = new LinearLayout(MainActivity.this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            float topR = entry.firstInGroup ? 20f : 4f;
            float botR = entry.lastInGroup ? 20f : 4f;
            row.setBackground(theme.cardItemBackground(topR, botR, theme.card));
            row.setPadding(theme.dp(14), theme.dp(12), theme.dp(14), theme.dp(12));

            ImageView iconView = new ImageView(MainActivity.this);
            if (app.icon != null) {
                iconView.setImageDrawable(app.icon);
            } else {
                iconView.setImageResource(android.R.drawable.sym_def_app_icon);
            }
            iconView.setScaleType(ImageView.ScaleType.FIT_CENTER);
            LinearLayout.LayoutParams iconLp = new LinearLayout.LayoutParams(theme.dp(38), theme.dp(38));
            iconLp.rightMargin = theme.dp(14);
            row.addView(iconView, iconLp);

            LinearLayout texts = new LinearLayout(MainActivity.this);
            texts.setOrientation(LinearLayout.VERTICAL);

            TextView name = new TextView(MainActivity.this);
            name.setText(app.label);
            name.setTextColor(theme.textPrimary);
            name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
            name.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            name.setSingleLine(true);
            name.setEllipsize(TextUtils.TruncateAt.END);
            texts.addView(name);

            TextView pkgView = new TextView(MainActivity.this);
            pkgView.setText(app.pkg);
            pkgView.setTextColor(theme.textSecondary);
            pkgView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            pkgView.setSingleLine(true);
            pkgView.setEllipsize(TextUtils.TruncateAt.END);
            LinearLayout.LayoutParams pkgLp = new LinearLayout.LayoutParams(-1, -2);
            pkgLp.topMargin = theme.dp(2);
            texts.addView(pkgView, pkgLp);

            row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1f));

            TextView chip = buildDriverChip(savedVal == null ? "" : savedVal, false);
            LinearLayout.LayoutParams chipLp = new LinearLayout.LayoutParams(-2, -2);
            chipLp.leftMargin = theme.dp(10);
            row.addView(chip, chipLp);

            if (!isCustom) {
                row.setAlpha(0.95f);
            }
            wrapper.addView(row, new FrameLayout.LayoutParams(-1, -2));
            return wrapper;
        }
    }

    private void showAppDriverDialog(AppItem app) {
        String current = savedAppDrivers.get(app.propKey);
        int[] selected = {"turnip".equals(current) ? 1 : "qcom".equals(current) ? 2 : 0};
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(app.label)
                .setSingleChoiceItems(str.appChoiceLabels(), selected[0], (d, which) -> selected[0] = which)
                .setNegativeButton(str.cancel(), null)
                .setNeutralButton(str.saveOnly(), (d, w) -> saveAppVulkan(app, DRIVER_VALUES[selected[0]], false))
                .setPositiveButton(str.saveAndStop(), (d, w) -> new AlertDialog.Builder(this, theme.dialogTheme())
                        .setTitle(str.confirmStopTitle())
                        .setMessage(str.confirmStopMessage(app.pkg))
                        .setNegativeButton(str.cancel(), null)
                        .setPositiveButton(str.confirmStopAction(),
                                (d2, w2) -> saveAppVulkan(app, DRIVER_VALUES[selected[0]], true))
                        .show())
                .show();
    }

    // ---- Level 2b: Performance Mode Page ----

    private View buildPerformancePage() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS);

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        int hPad = theme.dp(18);
        container.setPadding(hPad, theme.dp(8), hPad, theme.dp(28));

        container.addView(buildSubPageHeader(str.perfTitle(), () -> navigateTo(PAGE_HOME), () -> reloadAll(true)));

        if (!bridgeState.bridgeReady && !busy) {
            container.addView(buildBanner(str.bridgeMissingBanner(), true), marginBottom(12));
        } else if (!bannerText.isEmpty()) {
            container.addView(buildBanner(bannerText, bannerIsError), marginBottom(12));
        }

        // Current status hero card
        LinearLayout heroCard = new LinearLayout(this);
        heroCard.setOrientation(LinearLayout.VERTICAL);
        heroCard.setBackground(theme.roundedRect(theme.card, 22));
        heroCard.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));

        TextView curLabel = new TextView(this);
        curLabel.setText(str.perfCurrentSection());
        curLabel.setTextColor(theme.textSecondary);
        curLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        heroCard.addView(curLabel);

        TextView curValue = new TextView(this);
        String activeGov = bridgeState.currentGovernor;
        curValue.setText(activeGov.isEmpty() ? "—"
                : "mixed".equals(activeGov) ? str.perfSubtitle(activeGov, true)
                : str.governorShortName(activeGov));
        curValue.setTextColor(theme.textPrimary);
        curValue.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        curValue.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams cvLp = new LinearLayout.LayoutParams(-1, -2);
        cvLp.topMargin = theme.dp(4);
        heroCard.addView(curValue, cvLp);

        String clusterDetail = str.perfClusterDetail(bridgeState.cpuPolicies, bridgeState.savedGovernor);
        if (!clusterDetail.isEmpty()) {
            TextView detail = new TextView(this);
            detail.setText(clusterDetail);
            detail.setTextColor(theme.textSecondary);
            detail.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            detail.setLineSpacing(theme.dp(2), 1.0f);
            LinearLayout.LayoutParams dLp = new LinearLayout.LayoutParams(-1, -2);
            dLp.topMargin = theme.dp(8);
            heroCard.addView(detail, dLp);
        }

        container.addView(heroCard, marginBottom(16));

        // Supported policies header
        TextView listHeader = new TextView(this);
        listHeader.setText(str.perfPoliciesSection());
        listHeader.setTextColor(theme.primary);
        listHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        listHeader.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        listHeader.setPadding(theme.dp(8), theme.dp(2), theme.dp(8), theme.dp(8));
        container.addView(listHeader);

        List<String> governors = bridgeState.availableGovernors;
        if (governors.isEmpty()) {
            TextView empty = new TextView(this);
            empty.setText(str.perfNoGovernors());
            empty.setTextColor(theme.textSecondary);
            empty.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            empty.setBackground(theme.roundedRect(theme.card, 20));
            empty.setPadding(theme.dp(18), theme.dp(18), theme.dp(18), theme.dp(18));
            container.addView(empty, marginBottom(16));
        } else {
            LinearLayout govGroup = new LinearLayout(this);
            govGroup.setOrientation(LinearLayout.VERTICAL);
            boolean canEdit = !busy && bridgeState.bridgeReady;
            for (int i = 0; i < governors.size(); i++) {
                String gov = governors.get(i);
                boolean selected = gov.equals(bridgeState.currentGovernor);
                float topR = i == 0 ? 22f : 4f;
                float botR = i == governors.size() - 1 ? 22f : 4f;
                govGroup.addView(buildGovernorRow(gov, selected, canEdit, topR, botR));
                if (i < governors.size() - 1) {
                    View gap = new View(this);
                    govGroup.addView(gap, new LinearLayout.LayoutParams(-1, theme.dp(2)));
                }
            }
            container.addView(govGroup, marginBottom(16));

            // Restore system default pill button
            TextView restoreBtn = new TextView(this);
            restoreBtn.setText(str.perfRestoreDefault());
            restoreBtn.setTextColor(canEdit ? theme.primary : theme.textSecondary);
            restoreBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f);
            restoreBtn.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            restoreBtn.setGravity(Gravity.CENTER);
            restoreBtn.setBackground(theme.pillRipple(theme.card, theme.divider));
            restoreBtn.setPadding(theme.dp(20), theme.dp(13), theme.dp(20), theme.dp(13));
            restoreBtn.setEnabled(canEdit);
            restoreBtn.setOnClickListener(v -> saveCpuGovernor(""));
            container.addView(restoreBtn, marginBottom(16));
        }

        TextView footer = new TextView(this);
        footer.setText(str.perfFooter());
        footer.setTextColor(theme.textSecondary);
        footer.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        footer.setLineSpacing(theme.dp(3), 1.0f);
        footer.setBackground(theme.roundedRect(theme.cardAlt, 18));
        footer.setPadding(theme.dp(16), theme.dp(14), theme.dp(16), theme.dp(14));
        container.addView(footer);

        scroll.addView(container);
        return scroll;
    }

    // ---- Level 2c: Touch Settings Page ----

    private View buildTouchPage() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS);

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        int hPad = theme.dp(18);
        container.setPadding(hPad, theme.dp(8), hPad, theme.dp(28));

        container.addView(buildSubPageHeader(str.touchTitle(), () -> navigateTo(PAGE_HOME), () -> reloadAll(true)));

        if (!bridgeState.bridgeReady && !busy) {
            container.addView(buildBanner(str.bridgeMissingBanner(), true), marginBottom(12));
        } else if (loadedOnce && !bridgeState.touchAvailable && !busy) {
            container.addView(buildBanner(str.touchUnavailableBanner(), true), marginBottom(12));
        } else if (!bannerText.isEmpty()) {
            container.addView(buildBanner(bannerText, bannerIsError), marginBottom(12));
        }

        boolean canControl = !busy && bridgeState.bridgeReady && bridgeState.touchAvailable;
        container.addView(buildGhostPatchCard(canControl), marginBottom(16));

        TextView summaryHeader = new TextView(this);
        summaryHeader.setText(str.touchSummarySection());
        summaryHeader.setTextColor(theme.primary);
        summaryHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        summaryHeader.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        summaryHeader.setPadding(theme.dp(8), theme.dp(2), theme.dp(8), theme.dp(8));
        container.addView(summaryHeader);

        String[][] infoRows = {
                {str.touchLabelHardware(), str.touchValueHardware(
                        bridgeState.touchHidId, bridgeState.touchDeviceId, bridgeState.touchIptsdRunning)},
                {str.touchLabelCoords(), str.touchValueCoords(
                        bridgeState.touchProfileSaved, bridgeState.touchProfileX, bridgeState.touchProfileY,
                        bridgeState.touchProfileXTop, bridgeState.touchProfileTopStart, bridgeState.touchProfileTopEnd)},
                {str.touchLabelThumb(), str.touchValueThumb(bridgeState.touchProfileSizeMax)},
                {str.touchLabelNoise(), str.touchValueNoise(
                        bridgeState.touchNoiseSaved, bridgeState.touchNoiseEnabled, bridgeState.touchNoiseInfo)},
                {str.touchLabelTap(), str.touchValueTap(
                        bridgeState.touchTapSaved, bridgeState.touchTapLine)},
        };
        LinearLayout summaryGroup = new LinearLayout(this);
        summaryGroup.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < infoRows.length; i++) {
            float topR = i == 0 ? 22f : 4f;
            float botR = i == infoRows.length - 1 ? 22f : 4f;
            summaryGroup.addView(buildTouchInfoRow(infoRows[i][0], infoRows[i][1], topR, botR));
            if (i < infoRows.length - 1) {
                View gap = new View(this);
                summaryGroup.addView(gap, new LinearLayout.LayoutParams(-1, theme.dp(2)));
            }
        }
        container.addView(summaryGroup, marginBottom(16));

        TextView actionHeader = new TextView(this);
        actionHeader.setText(str.touchActionsSection());
        actionHeader.setTextColor(theme.primary);
        actionHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        actionHeader.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        actionHeader.setPadding(theme.dp(8), theme.dp(2), theme.dp(8), theme.dp(8));
        container.addView(actionHeader);

        LinearLayout actionGroup = new LinearLayout(this);
        actionGroup.setOrientation(LinearLayout.VERTICAL);

        actionGroup.addView(buildCategoryRow(
                SettingsTheme.ICON_CALIBRATE,
                theme.touchBadgeBg,
                theme.touchBadgeFg,
                str.touchRecalibrateTitle(),
                str.touchRecalibrateSummary(),
                22f,
                4f,
                !busy,
                this::openCalibrationApp));

        View actionGap1 = new View(this);
        actionGroup.addView(actionGap1, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        boolean canExport = canControl
                && (bridgeState.touchProfileSaved || bridgeState.touchNoiseSaved || bridgeState.touchTapSaved);

        actionGroup.addView(buildCategoryRow(
                SettingsTheme.ICON_IMPORT,
                theme.vulkanBadgeBg,
                theme.vulkanBadgeFg,
                str.touchImportTitle(),
                str.touchImportSummary(),
                4f,
                4f,
                canControl,
                this::openTouchImportPicker));

        View actionGap2 = new View(this);
        actionGroup.addView(actionGap2, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        actionGroup.addView(buildCategoryRow(
                SettingsTheme.ICON_EXPORT,
                theme.perfBadgeBg,
                theme.perfBadgeFg,
                str.touchExportTitle(),
                str.touchExportSummary(canExport),
                4f,
                4f,
                canExport,
                this::prepareTouchExport));

        View actionGap3 = new View(this);
        actionGroup.addView(actionGap3, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        actionGroup.addView(buildCategoryRow(
                SettingsTheme.ICON_TRASH,
                theme.dangerBadgeBg,
                theme.dangerBadgeFg,
                str.touchClearTitle(),
                str.touchClearSummary(),
                4f,
                22f,
                canControl,
                this::confirmClearTouchCalibration));

        container.addView(actionGroup);

        scroll.addView(container);
        return scroll;
    }

    private View buildGhostPatchCard(boolean canControl) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(theme.cardItemBackground(22f, 22f, theme.card));
        card.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));
        card.setEnabled(canControl);
        card.setClickable(canControl);
        card.setFocusable(canControl);
        if (!canControl) {
            card.setAlpha(0.6f);
        }

        LinearLayout topRow = new LinearLayout(this);
        topRow.setOrientation(LinearLayout.HORIZONTAL);
        topRow.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout titleCol = new LinearLayout(this);
        titleCol.setOrientation(LinearLayout.VERTICAL);

        LinearLayout titleLine = new LinearLayout(this);
        titleLine.setOrientation(LinearLayout.HORIZONTAL);
        titleLine.setGravity(Gravity.CENTER_VERTICAL);

        TextView title = new TextView(this);
        title.setText(str.touchGhostTitle());
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        titleLine.addView(title);

        TextView badge = new TextView(this);
        badge.setText(str.touchGhostBadge(bridgeState.touchNoiseSaved, bridgeState.touchNoiseEnabled));
        badge.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f);
        badge.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        badge.setPadding(theme.dp(9), theme.dp(3), theme.dp(9), theme.dp(3));
        if (bridgeState.touchNoiseEnabled) {
            badge.setTextColor(theme.okFg);
            badge.setBackground(theme.roundedRect(theme.okBg, 999));
        } else {
            badge.setTextColor(theme.neutralChipFg);
            badge.setBackground(theme.roundedRect(theme.neutralChipBg, 999));
        }
        LinearLayout.LayoutParams badgeLp = new LinearLayout.LayoutParams(-2, -2);
        badgeLp.leftMargin = theme.dp(10);
        titleLine.addView(badge, badgeLp);
        titleCol.addView(titleLine);

        topRow.addView(titleCol, new LinearLayout.LayoutParams(0, -2, 1f));

        View sw = theme.switchView(this, bridgeState.touchNoiseEnabled, canControl);
        LinearLayout.LayoutParams swLp = new LinearLayout.LayoutParams(theme.dp(52), theme.dp(32));
        swLp.leftMargin = theme.dp(12);
        topRow.addView(sw, swLp);
        card.addView(topRow);

        TextView desc = new TextView(this);
        desc.setText(str.touchGhostDescription(
                bridgeState.touchNoiseSaved, bridgeState.touchNoiseEnabled, bridgeState.touchNoiseInfo));
        desc.setTextColor(theme.textSecondary);
        desc.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        desc.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams descLp = new LinearLayout.LayoutParams(-1, -2);
        descLp.topMargin = theme.dp(8);
        card.addView(desc, descLp);

        if (canControl) {
            card.setOnClickListener(v -> {
                if (!bridgeState.touchNoiseEnabled && !bridgeState.touchNoiseSaved) {
                    showNoPatchDialog();
                } else {
                    saveTouchGhostPatch(!bridgeState.touchNoiseEnabled);
                }
            });
        }
        return card;
    }

    private void showNoPatchDialog() {
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(str.touchNoPatchDialogTitle())
                .setMessage(str.touchNoPatchDialogMessage())
                .setNegativeButton(str.cancel(), null)
                .setPositiveButton(str.touchRecalibrateTitle(), (d, w) -> openCalibrationApp())
                .show();
    }

    private View buildTouchInfoRow(String labelText, String valueText,
                                   float topRadiusDp, float bottomRadiusDp) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setBackground(theme.cardItemBackground(topRadiusDp, bottomRadiusDp, theme.card));
        row.setPadding(theme.dp(18), theme.dp(13), theme.dp(18), theme.dp(13));

        TextView label = new TextView(this);
        label.setText(labelText);
        label.setTextColor(theme.textSecondary);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        row.addView(label);

        TextView value = new TextView(this);
        value.setText(valueText);
        value.setTextColor(theme.textPrimary);
        value.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f);
        value.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams valLp = new LinearLayout.LayoutParams(-1, -2);
        valLp.topMargin = theme.dp(4);
        row.addView(value, valLp);

        return row;
    }

    private View buildGovernorRow(String gov, boolean selected, boolean enabled,
                                  float topRadiusDp, float bottomRadiusDp) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        int fill = selected ? theme.primaryContainer : theme.card;
        row.setBackground(theme.cardItemBackground(topRadiusDp, bottomRadiusDp, fill));
        row.setPadding(theme.dp(16), theme.dp(15), theme.dp(16), theme.dp(15));
        row.setClickable(enabled);
        row.setEnabled(enabled);
        row.setOnClickListener(v -> {
            if (enabled) {
                saveCpuGovernor(gov);
            }
        });

        View radio = theme.radioView(this, selected);
        LinearLayout.LayoutParams radioLp = new LinearLayout.LayoutParams(theme.dp(22), theme.dp(22));
        radioLp.rightMargin = theme.dp(14);
        row.addView(radio, radioLp);

        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);

        TextView name = new TextView(this);
        name.setText(str.governorShortName(gov));
        name.setTextColor(selected ? theme.onPrimaryContainer : theme.textPrimary);
        name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        name.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        texts.addView(name);

        TextView desc = new TextView(this);
        desc.setText(str.governorDescription(gov));
        desc.setTextColor(selected ? theme.onPrimaryContainer : theme.textSecondary);
        desc.setAlpha(selected ? 0.85f : 1.0f);
        desc.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        LinearLayout.LayoutParams descLp = new LinearLayout.LayoutParams(-1, -2);
        descLp.topMargin = theme.dp(3);
        texts.addView(desc, descLp);

        row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1f));

        if (selected) {
            TextView tag = new TextView(this);
            tag.setText(str.activeTag());
            tag.setTextColor(theme.onPrimary);
            tag.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f);
            tag.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            tag.setBackground(theme.roundedRect(theme.primary, 999));
            tag.setPadding(theme.dp(10), theme.dp(4), theme.dp(10), theme.dp(4));
            LinearLayout.LayoutParams tagLp = new LinearLayout.LayoutParams(-2, -2);
            tagLp.leftMargin = theme.dp(8);
            row.addView(tag, tagLp);
        }

        return row;
    }

    // ---- Shared Modern UI Components ----

    private View buildSubPageHeader(String titleText, Runnable onBack, Runnable onRefresh) {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(0, theme.dp(4), 0, theme.dp(10));

        bar.addView(buildIconButton(SettingsTheme.ICON_BACK, str.backDesc(), onBack));

        TextView title = new TextView(this);
        title.setText(titleText);
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, -2, 1f);
        titleLp.leftMargin = theme.dp(8);
        titleLp.rightMargin = theme.dp(8);
        bar.addView(title, titleLp);

        bar.addView(buildIconButton(SettingsTheme.ICON_REFRESH, str.refreshDesc(), onRefresh));
        return bar;
    }

    private View buildIconButton(int iconKind, String contentDesc, Runnable onClick) {
        FrameLayout btn = new FrameLayout(this);
        btn.setBackground(theme.circleRipple(android.graphics.Color.TRANSPARENT));
        btn.setContentDescription(contentDesc);
        btn.setClickable(true);
        btn.setFocusable(true);
        btn.setOnClickListener(v -> onClick.run());
        View glyph = theme.iconView(this, iconKind, theme.textPrimary, 22);
        btn.addView(glyph, new FrameLayout.LayoutParams(theme.dp(22), theme.dp(22), Gravity.CENTER));
        btn.setLayoutParams(new LinearLayout.LayoutParams(theme.dp(44), theme.dp(44)));
        return btn;
    }

    private View buildSegmentButton(String label, boolean selected, boolean enabled, Runnable onClick) {
        TextView btn = new TextView(this);
        btn.setText(label);
        btn.setGravity(Gravity.CENTER);
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        btn.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        btn.setPadding(theme.dp(10), theme.dp(10), theme.dp(10), theme.dp(10));
        if (selected) {
            btn.setTextColor(theme.onPrimary);
            btn.setBackground(theme.pillRipple(theme.primary, 0));
        } else {
            btn.setTextColor(enabled ? theme.textPrimary : theme.textSecondary);
            btn.setBackground(theme.pillRipple(android.graphics.Color.TRANSPARENT, 0));
        }
        btn.setEnabled(enabled);
        btn.setOnClickListener(v -> {
            if (enabled) {
                onClick.run();
            }
        });
        return btn;
    }

    private TextView buildDriverChip(String driverValue, boolean fullLabel) {
        TextView chip = new TextView(this);
        chip.setText(fullLabel ? str.driverDisplayName(driverValue) : str.appDriverBadge(driverValue));
        chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        chip.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        chip.setPadding(theme.dp(11), theme.dp(5), theme.dp(11), theme.dp(5));
        if ("turnip".equals(driverValue)) {
            chip.setTextColor(theme.mesaChipFg);
            chip.setBackground(theme.roundedRect(theme.mesaChipBg, 999));
        } else if ("qcom".equals(driverValue)) {
            chip.setTextColor(theme.qcomChipFg);
            chip.setBackground(theme.roundedRect(theme.qcomChipBg, 999));
        } else {
            chip.setTextColor(theme.neutralChipFg);
            chip.setBackground(theme.roundedRect(theme.neutralChipBg, 999));
        }
        return chip;
    }

    private TextView buildBanner(String message, boolean isError) {
        TextView banner = new TextView(this);
        banner.setText(message);
        banner.setTextColor(isError ? theme.warnFg : theme.okFg);
        banner.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        banner.setLineSpacing(theme.dp(2), 1.0f);
        banner.setBackground(theme.roundedRect(isError ? theme.warnBg : theme.okBg, 14));
        banner.setPadding(theme.dp(14), theme.dp(10), theme.dp(14), theme.dp(10));
        return banner;
    }

    private LinearLayout.LayoutParams marginBottom(int dp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = theme.dp(dp);
        return lp;
    }
}
