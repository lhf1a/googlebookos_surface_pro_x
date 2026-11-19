package org.surface.advancedcontrol;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.Locale;

/**
 * Modern Material 3 / Material You System Update page replacing stock Settings -> System update.
 * Supports:
 *   - Delta / manifest OTA archives (.tar.gz) when super is unchanged (or full manifest with super)
 *   - Full GPT disk images (.img / .img.gz)
 *   - Explicit data retention choice for ALL update modes: Keep user data vs Clean install (wipe)
 *   - Live progress tracking, pending offline update management, and one-tap reboot.
 */
public final class SystemUpdateActivity extends Activity {
    private static final int REQ_PICK_PACKAGE = 501;

    private SettingsTheme theme;
    private Strings str;
    private OtaClient client;

    private FrameLayout rootFrame;
    private FrameLayout contentSlot;

    private OtaClient.OtaState otaState = new OtaClient.OtaState();
    private OtaClient.PackageInfo inspectedPackage = null;

    private boolean wipeDataSelected = false;
    private boolean busy = false;
    private String progressStage = "";
    private int progressPercent = 0;
    private String progressDetail = "";
    private String bannerText = "";
    private boolean bannerIsError = false;

    private TextView liveProgressLabelView;
    private TextView liveProgressPctView;
    private ProgressBar liveProgressBar;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        theme = new SettingsTheme(this);
        str = new Strings(Locale.getDefault());
        client = new OtaClient(this);
        theme.styleSystemBars(this);

        rootFrame = new FrameLayout(this);
        rootFrame.setBackgroundColor(theme.bg);
        contentSlot = new FrameLayout(this);
        rootFrame.addView(contentSlot, new FrameLayout.LayoutParams(-1, -1));
        setContentView(rootFrame);

        rootFrame.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
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

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_PACKAGE && resultCode == RESULT_OK && data != null && data.getData() != null) {
            inspectSelectedUri(data.getData());
        }
    }

    private void refreshState(boolean requestDaemonRefresh) {
        if (busy) {
            return;
        }
        busy = true;
        progressStage = "";
        progressDetail = "";
        renderPage();

        new Thread(() -> {
            OtaClient.OtaState next = client.loadState(requestDaemonRefresh);
            runOnUiThread(() -> {
                if (isDestroyed()) {
                    return;
                }
                otaState = next;
                busy = false;
                renderPage();
            });
        }, "spx-ota-refresh").start();
    }

    private void openPackagePicker() {
        if (busy || !otaState.bridgeReady) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "application/gzip",
            "application/x-gzip",
            "application/x-tar",
            "application/octet-stream",
            "*/*"
        });
        try {
            startActivityForResult(intent, REQ_PICK_PACKAGE);
        } catch (Exception e) {
            bannerText = str.otaErrorMessage(e.getMessage() != null ? e.getMessage() : "picker_failed");
            bannerIsError = true;
            renderPage();
        }
    }

    private void inspectSelectedUri(Uri uri) {
        if (busy) {
            return;
        }
        busy = true;
        inspectedPackage = null;
        wipeDataSelected = false;
        bannerText = "";
        bannerIsError = false;
        progressStage = "copying";
        progressPercent = 0;
        progressDetail = "";
        renderPage();

        new Thread(() -> {
            try {
                OtaClient.PackageInfo info = client.stageAndInspect(this, uri, this::postProgress);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    inspectedPackage = info;
                    busy = false;
                    progressStage = "";
                    renderPage();
                });
            } catch (Exception e) {
                String code = e.getMessage() != null ? e.getMessage().trim() : "inspect_failed";
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    inspectedPackage = null;
                    busy = false;
                    progressStage = "";
                    bannerText = str.otaErrorMessage(code);
                    bannerIsError = true;
                    renderPage();
                });
            }
        }, "spx-ota-inspect").start();
    }

    private void onInstallClicked() {
        if (busy || inspectedPackage == null || !otaState.bridgeReady) {
            return;
        }
        if (!inspectedPackage.superIncluded && !inspectedPackage.superCompatible) {
            bannerText = str.otaErrorMessage("super_baseline_mismatch");
            bannerIsError = true;
            renderPage();
            return;
        }
        if (wipeDataSelected) {
            new AlertDialog.Builder(this, theme.dialogTheme())
                    .setTitle(str.otaWipeConfirmTitle())
                    .setMessage(str.otaWipeConfirmMessage())
                    .setNegativeButton(str.cancel(), null)
                    .setPositiveButton(str.otaWipeConfirmPositive(), (d, w) -> startInstall())
                    .show();
            return;
        }
        startInstall();
    }

    private void startInstall() {
        if (busy || inspectedPackage == null) {
            return;
        }
        final String sha = inspectedPackage.packageSha256;
        final boolean wipe = wipeDataSelected;
        busy = true;
        bannerText = "";
        bannerIsError = false;
        progressStage = wipe || inspectedPackage.superIncluded ? "staging_offline" : "extracting";
        progressPercent = 10;
        progressDetail = "";
        renderPage();

        new Thread(() -> {
            try {
                client.installPackage(sha, wipe, this::postProgress);
                OtaClient.OtaState next = client.loadState(false);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    otaState = next;
                    inspectedPackage = null;
                    wipeDataSelected = false;
                    busy = false;
                    progressStage = "";
                    renderPage();
                });
            } catch (Exception e) {
                String code = e.getMessage() != null ? e.getMessage().trim() : "install_failed";
                OtaClient.OtaState next = client.loadState(false);
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    otaState = next;
                    busy = false;
                    progressStage = "";
                    bannerText = str.otaErrorMessage(code);
                    bannerIsError = true;
                    renderPage();
                });
            }
        }, "spx-ota-install").start();
    }

    private void cancelPendingUpdate() {
        if (busy) {
            return;
        }
        busy = true;
        bannerText = "";
        bannerIsError = false;
        renderPage();

        new Thread(() -> {
            try {
                OtaClient.OtaState next = client.cancelPending();
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    otaState = next;
                    inspectedPackage = null;
                    wipeDataSelected = false;
                    busy = false;
                    renderPage();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        return;
                    }
                    busy = false;
                    bannerText = str.otaErrorMessage(e.getMessage() != null ? e.getMessage() : "cancel_failed");
                    bannerIsError = true;
                    renderPage();
                });
            }
        }, "spx-ota-cancel").start();
    }

    private void dismissResultBanner() {
        if (busy) {
            return;
        }
        new Thread(() -> {
            try {
                OtaClient.OtaState next = client.clearResult();
                runOnUiThread(() -> {
                    if (!isDestroyed()) {
                        otaState = next;
                        renderPage();
                    }
                });
            } catch (Exception ignored) {
            }
        }, "spx-ota-clear").start();
    }

    private void confirmReboot(boolean isOffline) {
        if (busy) {
            return;
        }
        new AlertDialog.Builder(this, theme.dialogTheme())
                .setTitle(str.otaRebootConfirmTitle())
                .setMessage(str.otaRebootConfirmMessage(isOffline))
                .setNegativeButton(str.cancel(), null)
                .setPositiveButton(str.otaRebootNowButton(), (d, w) -> doReboot())
                .show();
    }

    private void doReboot() {
        busy = true;
        renderPage();
        new Thread(() -> {
            try {
                client.requestReboot();
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (!isDestroyed()) {
                        busy = false;
                        bannerText = str.otaErrorMessage(e.getMessage() != null ? e.getMessage() : "reboot_failed");
                        bannerIsError = true;
                        renderPage();
                    }
                });
            }
        }, "spx-ota-reboot").start();
    }

    private void postProgress(String stage, int pct, String detail) {
        runOnUiThread(() -> {
            if (isDestroyed()) {
                return;
            }
            progressStage = stage != null ? stage : "";
            progressPercent = pct;
            progressDetail = detail != null ? detail : "";
            if (liveProgressLabelView != null) {
                liveProgressLabelView.setText(str.otaProgressLabel(progressStage, progressDetail));
            }
            if (liveProgressPctView != null) {
                liveProgressPctView.setText(pct >= 0 ? (pct + "%") : "");
            }
            if (liveProgressBar != null) {
                if (pct < 0) {
                    liveProgressBar.setIndeterminate(true);
                } else {
                    liveProgressBar.setIndeterminate(false);
                    liveProgressBar.setProgress(Math.max(0, Math.min(100, pct)));
                }
            }
        });
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
        topBar.addView(buildIconButton(SettingsTheme.ICON_BACK, str.backDesc(), this::finish));
        View spacer = new View(this);
        topBar.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1f));
        topBar.addView(buildIconButton(SettingsTheme.ICON_REFRESH, str.refreshDesc(), () -> refreshState(true)));
        container.addView(topBar);

        // Large M3 Title
        TextView title = new TextView(this);
        title.setText(str.otaTitle());
        title.setTextColor(theme.textPrimary);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(-1, -2);
        titleLp.topMargin = theme.dp(6);
        titleLp.bottomMargin = theme.dp(18);
        container.addView(title, titleLp);

        if (!otaState.bridgeReady && !busy) {
            container.addView(buildBanner(str.otaBridgeUnavailable(), true), marginBottom(16));
        }
        if (!bannerText.isEmpty()) {
            container.addView(buildBanner(bannerText, bannerIsError), marginBottom(16));
        }

        // Hero System Version & Storage Card
        container.addView(buildHeroCard(), marginBottom(16));

        // Active / Pending / Result Status Cards
        if (otaState.pendingActive) {
            container.addView(buildPendingCard(), marginBottom(16));
        } else if ("APPLIED_PENDING_REBOOT".equals(otaState.lastStatus)) {
            container.addView(buildAppliedRebootCard(), marginBottom(16));
        } else if ("OK".equals(otaState.lastStatus)) {
            container.addView(buildLastSuccessCard(), marginBottom(16));
        } else if ("ERROR".equals(otaState.lastStatus)) {
            container.addView(buildLastErrorCard(), marginBottom(16));
        }

        // Live progress card when busy
        if (busy && !progressStage.isEmpty()) {
            container.addView(buildProgressCard(), marginBottom(16));
        }

        // Inspected Package + Data Mode Card
        if (inspectedPackage != null) {
            container.addView(buildSectionHeader(str.otaInspectSection()));
            container.addView(buildInspectedPackageCard(inspectedPackage), marginBottom(16));

            container.addView(buildSectionHeader(str.otaDataModeSection()));
            container.addView(buildDataRetentionCard(), marginBottom(16));

            container.addView(buildInstallActionRow(inspectedPackage), marginBottom(20));
        } else {
            // Select Local Package Card
            container.addView(buildSectionHeader(str.otaSelectSection()));
            container.addView(buildSelectPackageCard(), marginBottom(20));
        }

        // Footer
        TextView footer = new TextView(this);
        footer.setText(str.otaFooter());
        footer.setTextColor(theme.textSecondary);
        footer.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        footer.setLineSpacing(theme.dp(3), 1.0f);
        footer.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));
        container.addView(footer);

        // Constrain max width cleanly on large tablet/desktop displays
        int screenW = getResources().getDisplayMetrics().widthPixels;
        int maxW = theme.dp(680);
        int width = screenW > maxW ? maxW : -1;
        outer.addView(container, new LinearLayout.LayoutParams(width, -2));
        scroll.addView(outer, new FrameLayout.LayoutParams(-1, -2));
        contentSlot.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
    }

    private View buildHeroCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(20), theme.dp(20), theme.dp(20));
        card.setBackground(theme.roundedRect(theme.card, 26f));

        FrameLayout badge = new FrameLayout(this);
        badge.setBackground(theme.roundedRect(theme.otaBadgeBg, 28f));
        View icon = theme.iconView(this, SettingsTheme.ICON_UPDATE, theme.otaBadgeFg, 28);
        FrameLayout.LayoutParams ilp = new FrameLayout.LayoutParams(theme.dp(28), theme.dp(28));
        ilp.gravity = Gravity.CENTER;
        badge.addView(icon, ilp);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(theme.dp(56), theme.dp(56));
        blp.rightMargin = theme.dp(16);
        card.addView(badge, blp);

        LinearLayout textCol = new LinearLayout(this);
        textCol.setOrientation(LinearLayout.VERTICAL);

        TextView head = new TextView(this);
        head.setText(str.otaVersionLabel(otaState.currentVersion));
        head.setTextColor(theme.textPrimary);
        head.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        head.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        textCol.addView(head);

        TextView sub = new TextView(this);
        sub.setText(str.otaHeroSubtitle() + " · Android " + Build.VERSION.RELEASE);
        sub.setTextColor(theme.textSecondary);
        sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(-1, -2);
        slp.topMargin = theme.dp(3);
        textCol.addView(sub, slp);

        TextView meta = new TextView(this);
        String kLine = str.otaKernelLabel(otaState.currentKernel);
        if (!otaState.currentCommit.isEmpty()) {
            kLine += " · " + otaState.currentCommit;
        }
        if (otaState.espTotalMb > 0) {
            kLine += "\n" + str.otaStorageLabel(otaState.espFreeMb, otaState.espTotalMb, otaState.dataFreeMb);
        }
        meta.setText(kLine);
        meta.setTextColor(theme.textSecondary);
        meta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        meta.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams mlp = new LinearLayout.LayoutParams(-1, -2);
        mlp.topMargin = theme.dp(6);
        textCol.addView(meta, mlp);

        card.addView(textCol, new LinearLayout.LayoutParams(0, -2, 1f));
        return card;
    }

    private View buildPendingCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(18), theme.dp(20), theme.dp(18));
        card.setBackground(theme.roundedRect(theme.primaryContainer, 24f));

        TextView title = new TextView(this);
        title.setText(str.otaPendingTitle());
        title.setTextColor(theme.onPrimaryContainer);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        card.addView(title);

        TextView body = new TextView(this);
        body.setText(str.otaPendingBody(
                otaState.pendingVersion,
                otaState.pendingKind,
                otaState.pendingApplySuper,
                otaState.pendingWipeData));
        body.setTextColor(theme.onPrimaryContainer);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        body.setLineSpacing(theme.dp(3), 1.0f);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(-1, -2);
        blp.topMargin = theme.dp(6);
        blp.bottomMargin = theme.dp(14);
        card.addView(body, blp);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.addView(buildPillButton(
                str.otaRebootInstallButton(),
                theme.primary,
                theme.onPrimary,
                0,
                !busy,
                () -> confirmReboot(true)));
        View sp = new View(this);
        actions.addView(sp, new LinearLayout.LayoutParams(theme.dp(10), 1));
        actions.addView(buildPillButton(
                str.otaCancelPendingButton(),
                theme.card,
                theme.textPrimary,
                0,
                !busy,
                this::cancelPendingUpdate));
        card.addView(actions);
        return card;
    }

    private View buildAppliedRebootCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(18), theme.dp(20), theme.dp(18));
        card.setBackground(theme.roundedRect(theme.okBg, 24f));

        TextView title = new TextView(this);
        title.setText(str.otaAppliedRebootTitle());
        title.setTextColor(theme.okFg);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        card.addView(title);

        TextView body = new TextView(this);
        body.setText(str.otaAppliedRebootBody(otaState.lastVersion));
        body.setTextColor(theme.okFg);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        body.setLineSpacing(theme.dp(3), 1.0f);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(-1, -2);
        blp.topMargin = theme.dp(6);
        blp.bottomMargin = theme.dp(14);
        card.addView(body, blp);

        card.addView(buildPillButton(
                str.otaRebootNowButton(),
                theme.primary,
                theme.onPrimary,
                0,
                !busy,
                () -> confirmReboot(false)));
        return card;
    }

    private View buildLastSuccessCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setPadding(theme.dp(18), theme.dp(14), theme.dp(14), theme.dp(14));
        card.setBackground(theme.roundedRect(theme.okBg, 20f));

        View icon = theme.iconView(this, SettingsTheme.ICON_CHECK, theme.okFg, 22);
        LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(theme.dp(22), theme.dp(22));
        ilp.rightMargin = theme.dp(12);
        card.addView(icon, ilp);

        TextView tv = new TextView(this);
        tv.setText(str.otaLastSuccessTitle(otaState.lastVersion, otaState.lastWipeData));
        tv.setTextColor(theme.okFg);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        tv.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        card.addView(tv, new LinearLayout.LayoutParams(0, -2, 1f));

        card.addView(buildPillButton(str.otaDismissButton(), theme.card, theme.textPrimary, 0, true, this::dismissResultBanner));
        return card;
    }

    private View buildLastErrorCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));
        card.setBackground(theme.roundedRect(theme.warnBg, 20f));

        TextView title = new TextView(this);
        title.setText(str.otaLastErrorTitle());
        title.setTextColor(theme.warnFg);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        card.addView(title);

        TextView msg = new TextView(this);
        msg.setText(str.otaErrorMessage(otaState.lastError));
        msg.setTextColor(theme.warnFg);
        msg.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        LinearLayout.LayoutParams mlp = new LinearLayout.LayoutParams(-1, -2);
        mlp.topMargin = theme.dp(4);
        mlp.bottomMargin = theme.dp(10);
        card.addView(msg, mlp);

        card.addView(buildPillButton(str.otaDismissButton(), theme.card, theme.textPrimary, 0, true, this::dismissResultBanner));
        return card;
    }

    private View buildProgressCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(18), theme.dp(20), theme.dp(18));
        card.setBackground(theme.roundedRect(theme.card, 24f));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);

        liveProgressLabelView = new TextView(this);
        liveProgressLabelView.setText(str.otaProgressLabel(progressStage, progressDetail));
        liveProgressLabelView.setTextColor(theme.textPrimary);
        liveProgressLabelView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        liveProgressLabelView.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        header.addView(liveProgressLabelView, new LinearLayout.LayoutParams(0, -2, 1f));

        liveProgressPctView = new TextView(this);
        liveProgressPctView.setText(progressPercent >= 0 ? (progressPercent + "%") : "");
        liveProgressPctView.setTextColor(theme.primary);
        liveProgressPctView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        liveProgressPctView.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        header.addView(liveProgressPctView);
        card.addView(header);

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
        LayerDrawable ld = new LayerDrawable(new Drawable[] {track, clip});
        ld.setId(0, android.R.id.background);
        ld.setId(1, android.R.id.progress);
        liveProgressBar.setProgressDrawable(ld);

        LinearLayout.LayoutParams plp = new LinearLayout.LayoutParams(-1, theme.dp(8));
        plp.topMargin = theme.dp(12);
        card.addView(liveProgressBar, plp);
        return card;
    }

    private View buildSelectPackageCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(20), theme.dp(20), theme.dp(20));
        card.setBackground(theme.roundedRect(theme.card, 24f));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        FrameLayout badge = new FrameLayout(this);
        badge.setBackground(theme.roundedRect(theme.cardAlt, 20f));
        View icon = theme.iconView(this, SettingsTheme.ICON_PACKAGE, theme.primary, 22);
        FrameLayout.LayoutParams ilp = new FrameLayout.LayoutParams(theme.dp(22), theme.dp(22));
        ilp.gravity = Gravity.CENTER;
        badge.addView(icon, ilp);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(theme.dp(42), theme.dp(42));
        blp.rightMargin = theme.dp(14);
        row.addView(badge, blp);

        LinearLayout textCol = new LinearLayout(this);
        textCol.setOrientation(LinearLayout.VERTICAL);
        TextView t = new TextView(this);
        t.setText(str.otaSelectTitle());
        t.setTextColor(theme.textPrimary);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        textCol.addView(t);

        TextView d = new TextView(this);
        d.setText(str.otaSelectHint());
        d.setTextColor(theme.textSecondary);
        d.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        d.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(-1, -2);
        dlp.topMargin = theme.dp(4);
        textCol.addView(d, dlp);
        row.addView(textCol, new LinearLayout.LayoutParams(0, -2, 1f));
        card.addView(row);

        LinearLayout btnRow = new LinearLayout(this);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams brlp = new LinearLayout.LayoutParams(-1, -2);
        brlp.topMargin = theme.dp(16);
        btnRow.addView(buildPillButton(
                str.otaSelectButton(),
                theme.primary,
                theme.onPrimary,
                0,
                !busy && otaState.bridgeReady,
                this::openPackagePicker));
        card.addView(btnRow, brlp);
        return card;
    }

    private View buildInspectedPackageCard(OtaClient.PackageInfo pkg) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(theme.dp(20), theme.dp(18), theme.dp(20), theme.dp(18));
        card.setBackground(theme.roundedRect(theme.card, 24f));

        // Badge pill
        TextView chip = new TextView(this);
        chip.setText(str.otaPackageBadge(pkg.packageKind, pkg.superIncluded, pkg.espMode));
        chip.setTextColor(pkg.superIncluded ? theme.qcomChipFg : theme.mesaChipFg);
        chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        chip.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        chip.setPadding(theme.dp(12), theme.dp(5), theme.dp(12), theme.dp(5));
        chip.setBackground(theme.roundedRect(pkg.superIncluded ? theme.qcomChipBg : theme.mesaChipBg, 999f));
        LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(-2, -2);
        clp.bottomMargin = theme.dp(10);
        card.addView(chip, clp);

        TextView name = new TextView(this);
        name.setText(pkg.displayName);
        name.setTextColor(theme.textPrimary);
        name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
        name.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        card.addView(name);

        TextView meta = new TextView(this);
        meta.setText(str.otaPackageMeta(pkg.version, pkg.fileCount, pkg.packageBytes, pkg.packageSha256));
        meta.setTextColor(theme.textSecondary);
        meta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
        meta.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams mlp = new LinearLayout.LayoutParams(-1, -2);
        mlp.topMargin = theme.dp(4);
        card.addView(meta, mlp);

        TextView superNote = new TextView(this);
        superNote.setText(str.otaSuperSummary(pkg.superIncluded, pkg.superCompatible, pkg.superBytes));
        boolean badCompat = !pkg.superIncluded && !pkg.superCompatible;
        superNote.setTextColor(badCompat ? theme.warnFg : theme.textSecondary);
        superNote.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        superNote.setLineSpacing(theme.dp(2), 1.0f);
        superNote.setPadding(theme.dp(12), theme.dp(10), theme.dp(12), theme.dp(10));
        superNote.setBackground(theme.roundedRect(badCompat ? theme.warnBg : theme.cardAlt, 14f));
        LinearLayout.LayoutParams snlp = new LinearLayout.LayoutParams(-1, -2);
        snlp.topMargin = theme.dp(12);
        card.addView(superNote, snlp);

        return card;
    }

    private View buildDataRetentionCard() {
        LinearLayout group = new LinearLayout(this);
        group.setOrientation(LinearLayout.VERTICAL);

        group.addView(buildRadioRow(
                !wipeDataSelected,
                str.otaKeepDataTitle(),
                str.otaKeepDataSummary(),
                24f,
                4f,
                false,
                () -> {
                    if (!busy && wipeDataSelected) {
                        wipeDataSelected = false;
                        renderPage();
                    }
                }));

        View gap = new View(this);
        group.addView(gap, new LinearLayout.LayoutParams(-1, theme.dp(2)));

        group.addView(buildRadioRow(
                wipeDataSelected,
                str.otaWipeDataTitle(),
                str.otaWipeDataSummary(),
                4f,
                24f,
                true,
                () -> {
                    if (!busy && !wipeDataSelected) {
                        wipeDataSelected = true;
                        renderPage();
                    }
                }));

        if (wipeDataSelected) {
            LinearLayout warn = new LinearLayout(this);
            warn.setOrientation(LinearLayout.HORIZONTAL);
            warn.setGravity(Gravity.CENTER_VERTICAL);
            warn.setPadding(theme.dp(14), theme.dp(12), theme.dp(14), theme.dp(12));
            warn.setBackground(theme.roundedRect(theme.warnBg, 16f));
            View wi = theme.iconView(this, SettingsTheme.ICON_WARNING, theme.warnFg, 20);
            LinearLayout.LayoutParams wilp = new LinearLayout.LayoutParams(theme.dp(20), theme.dp(20));
            wilp.rightMargin = theme.dp(10);
            warn.addView(wi, wilp);
            TextView wt = new TextView(this);
            wt.setText(str.otaWipeWarningBanner());
            wt.setTextColor(theme.warnFg);
            wt.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f);
            wt.setLineSpacing(theme.dp(2), 1.0f);
            warn.addView(wt, new LinearLayout.LayoutParams(0, -2, 1f));
            LinearLayout.LayoutParams wlp = new LinearLayout.LayoutParams(-1, -2);
            wlp.topMargin = theme.dp(10);
            group.addView(warn, wlp);
        }

        return group;
    }

    private View buildRadioRow(
            boolean checked,
            String titleText,
            String summaryText,
            float topRadius,
            float bottomRadius,
            boolean dangerAccent,
            Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(theme.dp(18), theme.dp(16), theme.dp(18), theme.dp(16));
        row.setBackground(theme.cardItemBackground(topRadius, bottomRadius, theme.card));
        row.setClickable(true);
        row.setFocusable(true);
        row.setOnClickListener(v -> onClick.run());

        View radio = theme.radioView(this, checked);
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(theme.dp(22), theme.dp(22));
        rlp.rightMargin = theme.dp(16);
        row.addView(radio, rlp);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);

        TextView t = new TextView(this);
        t.setText(titleText);
        t.setTextColor(dangerAccent && checked ? theme.dangerBadgeFg : theme.textPrimary);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15.5f);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        col.addView(t);

        TextView s = new TextView(this);
        s.setText(summaryText);
        s.setTextColor(theme.textSecondary);
        s.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        s.setLineSpacing(theme.dp(2), 1.0f);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(-1, -2);
        slp.topMargin = theme.dp(3);
        col.addView(s, slp);

        row.addView(col, new LinearLayout.LayoutParams(0, -2, 1f));
        return row;
    }

    private View buildInstallActionRow(OtaClient.PackageInfo pkg) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        boolean willBeOffline = pkg.superIncluded || wipeDataSelected || !"manifest".equals(pkg.packageKind);
        boolean canInstall = !busy && otaState.bridgeReady && (pkg.superIncluded || pkg.superCompatible);
        int bg = wipeDataSelected ? theme.dangerBadgeFg : theme.primary;
        int fg = wipeDataSelected ? (theme.dark ? 0xFF3E0A06 : 0xFFFFFFFF) : theme.onPrimary;

        row.addView(buildPillButton(
                str.otaInstallButton(willBeOffline, wipeDataSelected),
                bg,
                fg,
                0,
                canInstall,
                this::onInstallClicked));

        View sp = new View(this);
        row.addView(sp, new LinearLayout.LayoutParams(theme.dp(10), 1));

        row.addView(buildPillButton(
                str.otaChangePackageButton(),
                theme.card,
                theme.textPrimary,
                theme.divider,
                !busy,
                this::openPackagePicker));

        View sp2 = new View(this);
        row.addView(sp2, new LinearLayout.LayoutParams(theme.dp(8), 1));

        row.addView(buildPillButton(
                str.otaClearSelectionButton(),
                theme.cardAlt,
                theme.textSecondary,
                0,
                !busy,
                () -> {
                    inspectedPackage = null;
                    wipeDataSelected = false;
                    bannerText = "";
                    renderPage();
                }));
        return row;
    }

    private View buildSectionHeader(String text) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(theme.primary);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        tv.setPadding(theme.dp(6), theme.dp(2), theme.dp(6), theme.dp(10));
        return tv;
    }

    private View buildBanner(String text, boolean isError) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(isError ? theme.warnFg : theme.okFg);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        tv.setLineSpacing(theme.dp(2), 1.0f);
        tv.setPadding(theme.dp(16), theme.dp(12), theme.dp(16), theme.dp(12));
        tv.setBackground(theme.roundedRect(isError ? theme.warnBg : theme.okBg, 16f));
        return tv;
    }

    private View buildIconButton(int iconKind, String desc, Runnable onClick) {
        FrameLayout btn = new FrameLayout(this);
        btn.setBackground(theme.circleRipple(0x00000000));
        btn.setContentDescription(desc);
        btn.setClickable(true);
        btn.setFocusable(true);
        btn.setOnClickListener(v -> onClick.run());
        View icon = theme.iconView(this, iconKind, theme.textPrimary, 22);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(theme.dp(22), theme.dp(22));
        lp.gravity = Gravity.CENTER;
        btn.addView(icon, lp);
        return new WrapperFrame(btn, theme.dp(44), theme.dp(44));
    }

    private View buildPillButton(
            String label,
            int fillColor,
            int textColor,
            int strokeColor,
            boolean enabled,
            Runnable onClick) {
        TextView tv = new TextView(this);
        tv.setText(label);
        tv.setTextColor(textColor);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        tv.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        tv.setPadding(theme.dp(18), theme.dp(11), theme.dp(18), theme.dp(11));
        tv.setBackground(theme.pillRipple(fillColor, strokeColor));
        tv.setEnabled(enabled);
        tv.setAlpha(enabled ? 1.0f : 0.48f);
        tv.setClickable(enabled);
        tv.setFocusable(enabled);
        if (enabled) {
            tv.setOnClickListener(v -> onClick.run());
        }
        return tv;
    }

    private LinearLayout.LayoutParams marginBottom(int dpVal) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = theme.dp(dpVal);
        return lp;
    }

    private static final class WrapperFrame extends FrameLayout {
        WrapperFrame(View child, int w, int h) {
            super(child.getContext());
            addView(child, new FrameLayout.LayoutParams(w, h));
            setLayoutParams(new LinearLayout.LayoutParams(w, h));
        }
    }
}
