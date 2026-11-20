package org.surface.advancedcontrol;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.Locale;

/**
 * Material 3 / Material You Internal Disk Fresh Installer page.
 * Allows clean installation of the running USB-booted system onto an internal non-USB SSD
 * without preserving old internal data and without migrating USB userdata.
 */
public final class InternalDiskInstallActivity extends Activity {
    private SettingsTheme theme;
    private DiskInstallStrings str;
    private DiskInstallClient client;

    private FrameLayout rootFrame;
    private FrameLayout contentSlot;

    private DiskInstallPolicy.DiskState diskState = new DiskInstallPolicy.DiskState();
    private String selectedDevice = "";
    private boolean acknowledged = false;
    private boolean loading = false;
    private boolean installing = false;
    private boolean poweroffRequested = false;

    private String progressStage = "";
    private int progressPercent = 0;
    private String progressDetail = "";
    private DiskInstallPolicy.InstallResult installResult = null;
    private String bannerText = "";
    private boolean bannerIsError = false;

    private TextView liveProgressLabelView;
    private TextView liveProgressPctView;
    private ProgressBar liveProgressBar;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        theme = new SettingsTheme(this);
        str = new DiskInstallStrings(getResources().getConfiguration().getLocales().get(0));
        client = new DiskInstallClient(this);
        theme.styleSystemBars(this);

        rootFrame = new FrameLayout(this);
        rootFrame.setBackgroundColor(theme.bg);
        contentSlot = new FrameLayout(this);
        rootFrame.addView(contentSlot, new FrameLayout.LayoutParams(-1, -1));
        setContentView(rootFrame);

        rootFrame.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                contentSlot.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                contentSlot.setPadding(
                        insets.getSystemWindowInsetLeft(),
                        insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom());
            }
            return insets;
        });

        renderPage();
        refreshState(true);
    }

    @Override public void onBackPressed() {
        if (installing) {
            return;
        }
        super.onBackPressed();
    }

    private void setKeepScreenOn(boolean on) {
        if (on) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    private int readLowBatteryPercentIfUnplugged() {
        try {
            Intent bat = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (bat == null) {
                return -1;
            }
            int status = bat.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            boolean charging = status == BatteryManager.BATTERY_STATUS_CHARGING
                    || status == BatteryManager.BATTERY_STATUS_FULL;
            int plugged = bat.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
            if (charging || plugged != 0) {
                return -1;
            }
            int level = bat.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = bat.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            if (level >= 0 && scale > 0) {
                int pct = (level * 100) / scale;
                return pct < 30 ? pct : -1;
            }
        } catch (Exception ignored) {
        }
        return -1;
    }

    private DiskInstallPolicy.TargetDisk findSelectedTarget() {
        for (DiskInstallPolicy.TargetDisk t : diskState.targets) {
            if (t.device.equals(selectedDevice)) {
                return t;
            }
        }
        for (DiskInstallPolicy.TargetDisk t : diskState.targets) {
            if (t.enoughSpace) {
                return t;
            }
        }
        return diskState.targets.isEmpty() ? null : diskState.targets.get(0);
    }

    private void refreshState(boolean requestDaemonRefresh) {
        if (loading || installing) {
            return;
        }
        loading = true;
        bannerText = "";
        bannerIsError = false;
        renderPage();

        new Thread(() -> {
            DiskInstallPolicy.DiskState next = client.loadState(requestDaemonRefresh);
            runOnUiThread(() -> {
                if (isDestroyed()) {
                    return;
                }
                diskState = next;
                DiskInstallPolicy.TargetDisk chosen = findSelectedTarget();
                selectedDevice = chosen != null ? chosen.device : "";
                loading = false;
                renderPage();
            });
        }, "spx-disk-refresh").start();
    }

    private void confirmAndStartInstall() {
        if (loading || installing || !diskState.bridgeReady || !diskState.eligible || !acknowledged) {
            return;
        }
        DiskInstallPolicy.TargetDisk target = findSelectedTarget();
        if (target == null || !target.enoughSpace) {
            return;
        }
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(str.confirmDialogTitle())
                .setMessage(str.confirmDialogMessage(target.model, target.device, target.bytes))
                .setNegativeButton(str.cancel(), null)
                .setPositiveButton(str.confirmDialogPositive(), (d, w) -> startInstall(target.device))
                .show();
    }

    private void startInstall(String targetDev) {
        if (installing) {
            return;
        }
        installing = true;
        installResult = null;
        bannerText = "";
        bannerIsError = false;
        progressStage = "preparing";
        progressPercent = 2;
        progressDetail = targetDev;
        setKeepScreenOn(true);
        renderPage();

        new Thread(() -> {
            try {
                DiskInstallPolicy.InstallResult res = client.installToDisk(targetDev, this::postProgress);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    installing = false;
                    setKeepScreenOn(false);
                    installResult = res;
                    progressStage = "done";
                    progressPercent = 100;
                    progressDetail = res.targetDev;
                    bannerText = "";
                    bannerIsError = false;
                    renderPage();
                });
            } catch (Exception e) {
                String code = e.getMessage() != null ? e.getMessage().trim() : "install_failed";
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    installing = false;
                    setKeepScreenOn(false);
                    progressStage = "";
                    bannerText = str.errorMessage(code);
                    bannerIsError = true;
                    renderPage();
                });
            }
        }, "spx-disk-install").start();
    }

    private void postProgress(String stage, int pct, String detail) {
        runOnUiThread(() -> {
            if (isDestroyed()) {
                return;
            }
            progressStage = stage;
            progressPercent = pct;
            progressDetail = detail;
            if (liveProgressLabelView != null) {
                liveProgressLabelView.setText(str.progressLabel(stage, detail));
            }
            if (liveProgressPctView != null) {
                liveProgressPctView.setText(pct >= 0 ? (pct + "%") : "");
            }
            if (liveProgressBar != null) {
                liveProgressBar.setIndeterminate(pct < 0);
                if (pct >= 0) {
                    liveProgressBar.setProgress(Math.max(0, Math.min(100, pct)));
                }
            }
        });
    }

    private void confirmPowerOff() {
        if (installing || poweroffRequested) {
            return;
        }
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(str.powerOffDialogTitle())
                .setMessage(str.powerOffDialogMessage())
                .setNegativeButton(str.cancel(), null)
                .setPositiveButton(str.powerOffNowButton(), (d, w) -> triggerPowerOff())
                .show();
    }

    private void triggerPowerOff() {
        if (poweroffRequested) {
            return;
        }
        poweroffRequested = true;
        bannerText = str.shuttingDownBanner();
        bannerIsError = false;
        renderPage();

        new Thread(() -> {
            try {
                client.requestPowerOff();
            } catch (Exception e) {
                String code = e.getMessage() != null ? e.getMessage().trim() : "poweroff_failed";
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    poweroffRequested = false;
                    bannerText = str.errorMessage(code);
                    bannerIsError = true;
                    renderPage();
                });
            }
        }, "spx-disk-poweroff").start();
    }

    // ---- Rendering ----

    private void renderPage() {
        contentSlot.removeAllViews();
        liveProgressLabelView = null;
        liveProgressPctView = null;
        liveProgressBar = null;

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS);

        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setGravity(Gravity.CENTER_HORIZONTAL);

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        int hPad = theme.dp(20);
        container.setPadding(hPad, theme.dp(12), hPad, theme.dp(32));

        // Top App Bar
        LinearLayout topBar = new LinearLayout(this);
        topBar.setOrientation(LinearLayout.HORIZONTAL);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        View backBtn = buildIconButton(SettingsTheme.ICON_BACK, str.backDesc(), () -> {
            if (!installing) {
                finish();
            }
        });
        backBtn.setEnabled(!installing);
        backBtn.setAlpha(installing ? 0.4f : 1f);
        topBar.addView(backBtn);
        topBar.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1f));
        View refreshBtn = buildIconButton(SettingsTheme.ICON_REFRESH, str.refreshDesc(), () -> refreshState(true));
        refreshBtn.setEnabled(!loading && !installing);
        refreshBtn.setAlpha((loading || installing) ? 0.4f : 1f);
        topBar.addView(refreshBtn);
        container.addView(topBar);

        // Large M3 Title
        TextView title = new TextView(this);
        title.setText(str.title());
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(-1, -2);
        titleLp.topMargin = theme.dp(6);
        container.addView(title, titleLp);

        TextView subtitle = new TextView(this);
        subtitle.setText(str.pageSubtitle());
        subtitle.setTextColor(theme.textSecondary);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        subtitle.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(-1, -2);
        subLp.topMargin = theme.dp(4);
        subLp.bottomMargin = theme.dp(18);
        container.addView(subtitle, subLp);

        if (loading) {
            container.addView(buildBanner(str.loadingStatus(), false), marginBottom(16));
        } else if (!diskState.bridgeReady) {
            container.addView(buildBanner(str.bridgeMissingBanner(), true), marginBottom(16));
        }
        if (!bannerText.isEmpty()) {
            container.addView(buildBanner(bannerText, bannerIsError), marginBottom(16));
        }

        // 1. Boot Environment Card
        container.addView(buildSectionHeader(str.bootSourceSection()));
        container.addView(buildBootSourceCard(), marginBottom(16));

        // 2. Completion Card (after successful install)
        if (installResult != null) {
            container.addView(buildDoneCard(installResult), marginBottom(16));
        } else if (installing) {
            // 3. Live Progress Card
            container.addView(buildProgressCard(), marginBottom(16));
        } else if (diskState.bridgeReady) {
            if (!diskState.eligible) {
                container.addView(buildBanner(str.ineligibleBanner(diskState.ineligibleReason), true), marginBottom(16));
            } else {
                // Low battery notice if unplugged < 30%
                int lowBat = readLowBatteryPercentIfUnplugged();
                if (lowBat >= 0) {
                    container.addView(buildBanner(str.batteryWarning(lowBat), true), marginBottom(16));
                }

                // Target Internal Disk Card(s)
                container.addView(buildSectionHeader(str.targetSection()));
                container.addView(buildTargetDiskList(), marginBottom(16));

                // Plan summary card
                DiskInstallPolicy.TargetDisk sel = findSelectedTarget();
                if (sel != null) {
                    container.addView(buildSectionHeader(str.planSection()));
                    container.addView(buildPlanCard(sel), marginBottom(16));
                }

                // Danger Warning + Explicit Acknowledgement + Install Button
                container.addView(buildDangerCard(), marginBottom(14));
                container.addView(buildAcknowledgeRow(), marginBottom(16));
                container.addView(buildStartButton(sel != null && sel.enoughSpace), marginBottom(20));
            }
        }

        // Footer
        TextView footer = new TextView(this);
        footer.setText(str.footer());
        footer.setTextColor(theme.textSecondary);
        footer.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        footer.setLineSpacing(theme.dp(3), 1.0f);
        footer.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));
        container.addView(footer);

        int screenW = getResources().getDisplayMetrics().widthPixels;
        int maxW = theme.dp(680);
        int width = screenW > maxW ? maxW : -1;
        outer.addView(container, new LinearLayout.LayoutParams(width, -2));
        scroll.addView(outer, new FrameLayout.LayoutParams(-1, -2));
        contentSlot.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
    }

    private View buildBootSourceCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));
        card.setBackground(theme.roundedRect(theme.card, 22f));

        boolean isUsb = diskState.bootIsUsb;
        int bgColor = isUsb ? theme.okBg : theme.cardAlt;
        int fgColor = isUsb ? theme.okFg : theme.textSecondary;

        FrameLayout badge = new FrameLayout(this);
        badge.setBackground(theme.roundedRect(bgColor, 999f));
        View icon = theme.iconView(this, isUsb ? SettingsTheme.ICON_CHECK : SettingsTheme.ICON_DISK, fgColor, 22);
        badge.addView(icon, new FrameLayout.LayoutParams(theme.dp(22), theme.dp(22), Gravity.CENTER));
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(theme.dp(44), theme.dp(44));
        blp.rightMargin = theme.dp(14);
        card.addView(badge, blp);

        TextView tv = new TextView(this);
        if (!diskState.bridgeReady) {
            tv.setText(loading ? str.loadingStatus() : str.bootSourceUnknown());
        } else if (diskState.bootIsUsb) {
            tv.setText(str.bootSourceUsb(diskState.bootDisk));
        } else if (!diskState.bootDisk.isEmpty()) {
            tv.setText(str.bootSourceInternal(diskState.bootDisk, diskState.bootTransport));
        } else {
            tv.setText(str.bootSourceUnknown());
        }
        tv.setTextColor(theme.textPrimary);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        tv.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        card.addView(tv, new LinearLayout.LayoutParams(0, -2, 1f));
        return card;
    }

    private View buildTargetDiskList() {
        LinearLayout group = new LinearLayout(this);
        group.setOrientation(LinearLayout.VERTICAL);
        int count = diskState.targets.size();
        for (int i = 0; i < count; i++) {
            DiskInstallPolicy.TargetDisk disk = diskState.targets.get(i);
            float topR = (i == 0) ? 22f : 4f;
            float botR = (i == count - 1) ? 22f : 4f;
            boolean selected = disk.device.equals(selectedDevice);

            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(theme.cardItemBackground(topR, botR, theme.card));
            row.setPadding(theme.dp(16), theme.dp(16), theme.dp(16), theme.dp(16));
            row.setEnabled(disk.enoughSpace);
            row.setClickable(disk.enoughSpace);
            if (!disk.enoughSpace) {
                row.setAlpha(0.55f);
            } else {
                row.setOnClickListener(v -> {
                    selectedDevice = disk.device;
                    renderPage();
                });
            }

            View radio = theme.radioView(this, selected);
            LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(theme.dp(24), theme.dp(24));
            rlp.rightMargin = theme.dp(14);
            row.addView(radio, rlp);

            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);

            TextView name = new TextView(this);
            name.setText(disk.model);
            name.setTextColor(theme.textPrimary);
            name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
            name.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            col.addView(name);

            TextView meta = new TextView(this);
            meta.setText(str.targetSummary(disk.device, disk.bus, disk.bytes, disk.enoughSpace));
            meta.setTextColor(theme.textSecondary);
            meta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            LinearLayout.LayoutParams mlp = new LinearLayout.LayoutParams(-1, -2);
            mlp.topMargin = theme.dp(2);
            col.addView(meta, mlp);

            row.addView(col, new LinearLayout.LayoutParams(0, -2, 1f));
            group.addView(row);

            if (i < count - 1) {
                group.addView(new View(this), new LinearLayout.LayoutParams(-1, theme.dp(2)));
            }
        }
        return group;
    }

    private View buildPlanCard(DiskInstallPolicy.TargetDisk sel) {
        TextView tv = new TextView(this);
        tv.setText(str.planDetails(diskState.requiredSuperBytes, diskState.requiredEspBytes, sel.bytes));
        tv.setTextColor(theme.textPrimary);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        tv.setLineSpacing(theme.dp(4), 1.0f);
        tv.setBackground(theme.roundedRect(theme.card, 22f));
        tv.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));
        return tv;
    }

    private View buildDangerCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(theme.roundedRect(theme.warnBg, 22f));
        card.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));

        LinearLayout hdr = new LinearLayout(this);
        hdr.setOrientation(LinearLayout.HORIZONTAL);
        hdr.setGravity(Gravity.CENTER_VERTICAL);
        View warnIcon = theme.iconView(this, SettingsTheme.ICON_WARNING, theme.warnFg, 22);
        LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(theme.dp(22), theme.dp(22));
        ilp.rightMargin = theme.dp(10);
        hdr.addView(warnIcon, ilp);

        TextView title = new TextView(this);
        title.setText(str.dangerTitle());
        title.setTextColor(theme.warnFg);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        hdr.addView(title, new LinearLayout.LayoutParams(0, -2, 1f));
        card.addView(hdr);

        TextView body = new TextView(this);
        body.setText(str.dangerBody());
        body.setTextColor(theme.warnFg);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        body.setLineSpacing(theme.dp(3), 1.0f);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(-1, -2);
        blp.topMargin = theme.dp(8);
        card.addView(body, blp);

        return card;
    }

    private View buildAcknowledgeRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setBackground(theme.roundedRect(theme.card, 18f));
        row.setPadding(theme.dp(14), theme.dp(12), theme.dp(16), theme.dp(12));

        CheckBox cb = new CheckBox(this);
        cb.setChecked(acknowledged);
        cb.setOnCheckedChangeListener((btn, checked) -> {
            acknowledged = checked;
            renderPage();
        });
        row.addView(cb);

        TextView label = new TextView(this);
        label.setText(str.acknowledgeCheckbox());
        label.setTextColor(theme.textPrimary);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        label.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, -2, 1f);
        lp.leftMargin = theme.dp(8);
        row.addView(label, lp);

        row.setOnClickListener(v -> {
            acknowledged = !acknowledged;
            renderPage();
        });
        return row;
    }

    private View buildStartButton(boolean targetReady) {
        boolean canStart = acknowledged && targetReady && diskState.eligible && !loading && !installing;
        TextView btn = new TextView(this);
        btn.setText(str.startInstallButton());
        btn.setGravity(Gravity.CENTER);
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
        btn.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        btn.setTextColor(canStart ? theme.warnFg : theme.textSecondary);
        btn.setBackground(theme.pillRipple(canStart ? theme.warnBg : theme.cardAlt, canStart ? theme.warnFg : theme.divider));
        btn.setPadding(theme.dp(22), theme.dp(15), theme.dp(22), theme.dp(15));
        btn.setEnabled(canStart);
        btn.setClickable(canStart);
        btn.setAlpha(canStart ? 1f : 0.55f);
        if (canStart) {
            btn.setOnClickListener(v -> confirmAndStartInstall());
        }
        return btn;
    }

    private View buildProgressCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(theme.roundedRect(theme.card, 24f));
        card.setPadding(theme.dp(20), theme.dp(20), theme.dp(20), theme.dp(20));

        TextView notice = new TextView(this);
        notice.setText(str.installingNotice());
        notice.setTextColor(theme.warnFg);
        notice.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        notice.setLineSpacing(theme.dp(2), 1.0f);
        notice.setBackground(theme.roundedRect(theme.warnBg, 14f));
        notice.setPadding(theme.dp(14), theme.dp(10), theme.dp(14), theme.dp(10));
        card.addView(notice, marginBottom(16));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        liveProgressLabelView = new TextView(this);
        liveProgressLabelView.setText(str.progressLabel(progressStage, progressDetail));
        liveProgressLabelView.setTextColor(theme.textPrimary);
        liveProgressLabelView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f);
        liveProgressLabelView.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        row.addView(liveProgressLabelView, new LinearLayout.LayoutParams(0, -2, 1f));

        liveProgressPctView = new TextView(this);
        liveProgressPctView.setText(progressPercent >= 0 ? (progressPercent + "%") : "");
        liveProgressPctView.setTextColor(theme.primary);
        liveProgressPctView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f);
        liveProgressPctView.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        row.addView(liveProgressPctView);
        card.addView(row, marginBottom(12));

        liveProgressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        liveProgressBar.setMax(100);
        liveProgressBar.setIndeterminate(progressPercent < 0);
        if (progressPercent >= 0) {
            liveProgressBar.setProgress(Math.max(0, Math.min(100, progressPercent)));
        }
        GradientDrawable track = new GradientDrawable();
        track.setColor(theme.cardAlt);
        track.setCornerRadius(theme.dp(6));
        GradientDrawable fill = new GradientDrawable();
        fill.setColor(theme.primary);
        fill.setCornerRadius(theme.dp(6));
        ClipDrawable clip = new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL);
        LayerDrawable layers = new LayerDrawable(new Drawable[] {track, clip});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        liveProgressBar.setProgressDrawable(layers);
        card.addView(liveProgressBar, new LinearLayout.LayoutParams(-1, theme.dp(10)));

        return card;
    }

    private View buildDoneCard(DiskInstallPolicy.InstallResult res) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(theme.roundedRect(theme.okBg, 24f));
        card.setPadding(theme.dp(20), theme.dp(20), theme.dp(20), theme.dp(20));

        LinearLayout hdr = new LinearLayout(this);
        hdr.setOrientation(LinearLayout.HORIZONTAL);
        hdr.setGravity(Gravity.CENTER_VERTICAL);
        View okIcon = theme.iconView(this, SettingsTheme.ICON_CHECK, theme.okFg, 24);
        LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(theme.dp(24), theme.dp(24));
        ilp.rightMargin = theme.dp(10);
        hdr.addView(okIcon, ilp);

        TextView title = new TextView(this);
        title.setText(str.doneTitle());
        title.setTextColor(theme.okFg);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        hdr.addView(title);
        card.addView(hdr, marginBottom(12));

        TextView body = new TextView(this);
        body.setText(str.doneBody(res));
        body.setTextColor(theme.textPrimary);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        body.setLineSpacing(theme.dp(4), 1.0f);
        card.addView(body, marginBottom(18));

        TextView powerOffBtn = new TextView(this);
        powerOffBtn.setText(str.powerOffNowButton());
        powerOffBtn.setGravity(Gravity.CENTER);
        powerOffBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        powerOffBtn.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        powerOffBtn.setTextColor(theme.onPrimary);
        powerOffBtn.setBackground(theme.pillRipple(theme.primary, 0));
        powerOffBtn.setPadding(theme.dp(20), theme.dp(14), theme.dp(20), theme.dp(14));
        powerOffBtn.setEnabled(!poweroffRequested);
        powerOffBtn.setAlpha(poweroffRequested ? 0.55f : 1f);
        powerOffBtn.setOnClickListener(v -> confirmPowerOff());
        card.addView(powerOffBtn, marginBottom(10));

        TextView laterBtn = new TextView(this);
        laterBtn.setText(str.powerOffLaterButton());
        laterBtn.setGravity(Gravity.CENTER);
        laterBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f);
        laterBtn.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        laterBtn.setTextColor(theme.textPrimary);
        laterBtn.setBackground(theme.pillRipple(theme.card, theme.divider));
        laterBtn.setPadding(theme.dp(20), theme.dp(13), theme.dp(20), theme.dp(13));
        laterBtn.setEnabled(!poweroffRequested);
        laterBtn.setOnClickListener(v -> finish());
        card.addView(laterBtn);

        return card;
    }

    private View buildSectionHeader(String text) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(theme.primary);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.leftMargin = theme.dp(4);
        lp.bottomMargin = theme.dp(8);
        tv.setLayoutParams(lp);
        return tv;
    }

    private View buildBanner(String text, boolean isError) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(isError ? theme.warnFg : theme.onPrimaryContainer);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        tv.setLineSpacing(theme.dp(2), 1.0f);
        tv.setBackground(theme.roundedRect(isError ? theme.warnBg : theme.primaryContainer, 18f));
        tv.setPadding(theme.dp(16), theme.dp(12), theme.dp(16), theme.dp(12));
        return tv;
    }

    private View buildIconButton(int iconKind, String contentDesc, Runnable onClick) {
        FrameLayout btn = new FrameLayout(this);
        btn.setContentDescription(contentDesc);
        btn.setBackground(theme.circleRipple(theme.card));
        View glyph = theme.iconView(this, iconKind, theme.textPrimary, 20);
        btn.addView(glyph, new FrameLayout.LayoutParams(theme.dp(20), theme.dp(20), Gravity.CENTER));
        btn.setOnClickListener(v -> onClick.run());
        btn.setLayoutParams(new LinearLayout.LayoutParams(theme.dp(44), theme.dp(44)));
        return btn;
    }

    private LinearLayout.LayoutParams marginBottom(int dp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = theme.dp(dp);
        return lp;
    }
}
