package org.surface.advancedcontrol;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;

/** Modern Material 3 / Material You Settings visual tokens and vector icons. */
final class SettingsTheme {
    final boolean dark;
    final float density;

    final int bg;
    final int card;
    final int cardAlt;
    final int searchBg;
    final int primary;
    final int onPrimary;
    final int primaryContainer;
    final int onPrimaryContainer;
    final int textPrimary;
    final int textSecondary;
    final int divider;
    final int ripple;

    final int vulkanBadgeBg;
    final int vulkanBadgeFg;
    final int perfBadgeBg;
    final int perfBadgeFg;

    final int mesaChipBg;
    final int mesaChipFg;
    final int qcomChipBg;
    final int qcomChipFg;
    final int neutralChipBg;
    final int neutralChipFg;

    final int warnBg;
    final int warnFg;
    final int okBg;
    final int okFg;

    SettingsTheme(Context ctx) {
        int nightMask = ctx.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        this.dark = nightMask == Configuration.UI_MODE_NIGHT_YES;
        this.density = ctx.getResources().getDisplayMetrics().density;

        if (Build.VERSION.SDK_INT >= 31) {
            if (dark) {
                bg = ctx.getColor(android.R.color.system_neutral1_900);
                card = ctx.getColor(android.R.color.system_neutral1_800);
                cardAlt = ctx.getColor(android.R.color.system_neutral2_800);
                searchBg = ctx.getColor(android.R.color.system_neutral2_800);
                primary = ctx.getColor(android.R.color.system_accent1_200);
                onPrimary = ctx.getColor(android.R.color.system_accent1_900);
                primaryContainer = ctx.getColor(android.R.color.system_accent1_700);
                onPrimaryContainer = ctx.getColor(android.R.color.system_accent1_100);
                textPrimary = ctx.getColor(android.R.color.system_neutral1_50);
                textSecondary = ctx.getColor(android.R.color.system_neutral2_200);
                divider = Color.argb(38, 255, 255, 255);
                ripple = Color.argb(36, 255, 255, 255);
            } else {
                bg = ctx.getColor(android.R.color.system_neutral1_50);
                card = ctx.getColor(android.R.color.system_neutral1_0);
                cardAlt = ctx.getColor(android.R.color.system_neutral2_50);
                searchBg = ctx.getColor(android.R.color.system_neutral1_0);
                primary = ctx.getColor(android.R.color.system_accent1_600);
                onPrimary = Color.WHITE;
                primaryContainer = ctx.getColor(android.R.color.system_accent1_100);
                onPrimaryContainer = ctx.getColor(android.R.color.system_accent1_900);
                textPrimary = ctx.getColor(android.R.color.system_neutral1_900);
                textSecondary = ctx.getColor(android.R.color.system_neutral2_600);
                divider = Color.argb(26, 0, 0, 0);
                ripple = Color.argb(24, 0, 0, 0);
            }
        } else {
            if (dark) {
                bg = 0xFF111318;
                card = 0xFF1D2026;
                cardAlt = 0xFF262A32;
                searchBg = 0xFF242830;
                primary = 0xFFA8C7FA;
                onPrimary = 0xFF062E6F;
                primaryContainer = 0xFF0842A0;
                onPrimaryContainer = 0xFFD3E3FD;
                textPrimary = 0xFFE3E5EA;
                textSecondary = 0xFFA8B0BC;
                divider = 0x26FFFFFF;
                ripple = 0x24FFFFFF;
            } else {
                bg = 0xFFF2F4F8;
                card = 0xFFFFFFFF;
                cardAlt = 0xFFE9EEF6;
                searchBg = 0xFFFFFFFF;
                primary = 0xFF1B6EF3;
                onPrimary = 0xFFFFFFFF;
                primaryContainer = 0xFFD3E3FD;
                onPrimaryContainer = 0xFF041E49;
                textPrimary = 0xFF1A1C1E;
                textSecondary = 0xFF5E636B;
                divider = 0x1A000000;
                ripple = 0x18000000;
            }
        }

        if (dark) {
            vulkanBadgeBg = 0xFF1E3A5F;
            vulkanBadgeFg = 0xFF9ECAFF;
            perfBadgeBg = 0xFF3E2D13;
            perfBadgeFg = 0xFFFFCA7A;
            mesaChipBg = 0xFF1A3A4A;
            mesaChipFg = 0xFF8BE2FF;
            qcomChipBg = 0xFF472814;
            qcomChipFg = 0xFFFFB784;
            neutralChipBg = 0xFF2B303A;
            neutralChipFg = 0xFFB8C0CC;
            warnBg = 0xFF45241E;
            warnFg = 0xFFFFB4A9;
            okBg = 0xFF183829;
            okFg = 0xFF8CE0B0;
        } else {
            vulkanBadgeBg = 0xFFDCEBFF;
            vulkanBadgeFg = 0xFF1557B0;
            perfBadgeBg = 0xFFFFECD2;
            perfBadgeFg = 0xFFB85D00;
            mesaChipBg = 0xFFD9F2FF;
            mesaChipFg = 0xFF0B5785;
            qcomChipBg = 0xFFFFE5D4;
            qcomChipFg = 0xFFA64300;
            neutralChipBg = 0xFFECEFF4;
            neutralChipFg = 0xFF535B66;
            warnBg = 0xFFFCE8E6;
            warnFg = 0xFFA50E0E;
            okBg = 0xFFE3F5EC;
            okFg = 0xFF137333;
        }
    }

    int dp(float value) {
        return Math.round(value * density);
    }

    int dialogTheme() {
        return dark
                ? android.R.style.Theme_DeviceDefault_Dialog_Alert
                : android.R.style.Theme_DeviceDefault_Light_Dialog_Alert;
    }

    void styleSystemBars(Activity activity) {
        Window window = activity.getWindow();
        window.setStatusBarColor(bg);
        window.setNavigationBarColor(bg);
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController ctrl = window.getInsetsController();
            if (ctrl != null) {
                int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                ctrl.setSystemBarsAppearance(dark ? 0 : mask, mask);
            }
        } else {
            View decor = window.getDecorView();
            int flags = decor.getSystemUiVisibility();
            if (dark) {
                flags &= ~(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
            } else {
                flags |= View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            }
            decor.setSystemUiVisibility(flags);
        }
    }

    Drawable roundedRect(int fillColor, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fillColor);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }

    Drawable roundedRectStroke(int fillColor, int strokeColor, float strokeDp, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fillColor);
        d.setCornerRadius(dp(radiusDp));
        d.setStroke(Math.max(1, dp(strokeDp)), strokeColor);
        return d;
    }

    Drawable cardItemBackground(float topRadiusDp, float bottomRadiusDp, int fillColor) {
        GradientDrawable content = new GradientDrawable();
        content.setColor(fillColor);
        float t = dp(topRadiusDp);
        float b = dp(bottomRadiusDp);
        content.setCornerRadii(new float[] {t, t, t, t, b, b, b, b});

        GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadii(new float[] {t, t, t, t, b, b, b, b});
        return new RippleDrawable(ColorStateList.valueOf(ripple), content, mask);
    }

    Drawable circleRipple(int fillColor) {
        GradientDrawable content = new GradientDrawable();
        content.setShape(GradientDrawable.OVAL);
        content.setColor(fillColor);
        GradientDrawable mask = new GradientDrawable();
        mask.setShape(GradientDrawable.OVAL);
        mask.setColor(Color.WHITE);
        return new RippleDrawable(ColorStateList.valueOf(ripple), content, mask);
    }

    Drawable pillRipple(int fillColor, int strokeColor) {
        GradientDrawable content = new GradientDrawable();
        content.setColor(fillColor);
        content.setCornerRadius(dp(999));
        if (strokeColor != 0) {
            content.setStroke(Math.max(1, dp(1)), strokeColor);
        }
        GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadius(dp(999));
        return new RippleDrawable(ColorStateList.valueOf(ripple), content, mask);
    }

    // ---- Crisp resolution-independent vector icon Views ----

    static final int ICON_BACK = 1;
    static final int ICON_REFRESH = 2;
    static final int ICON_CHEVRON = 3;
    static final int ICON_VULKAN = 4;
    static final int ICON_PERF = 5;
    static final int ICON_SEARCH = 6;
    static final int ICON_CLEAR = 7;

    View iconView(Context ctx, int kind, int color, int sizeDp) {
        IconGlyphView v = new IconGlyphView(ctx, kind, color, density);
        v.setMinimumWidth(dp(sizeDp));
        v.setMinimumHeight(dp(sizeDp));
        return v;
    }

    View radioView(Context ctx, boolean checked) {
        return new RadioIndicatorView(ctx, checked, primary, textSecondary, density);
    }

    private static final class IconGlyphView extends View {
        private final int kind;
        private final float density;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        IconGlyphView(Context ctx, int kind, int color, float density) {
            super(ctx);
            this.kind = kind;
            this.density = density;
            paint.setColor(color);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStrokeJoin(Paint.Join.ROUND);
        }

        @Override protected void onDraw(Canvas c) {
            float w = getWidth();
            float h = getHeight();
            float s = Math.min(w, h);
            float cx = w / 2f;
            float cy = h / 2f;
            paint.setStrokeWidth(Math.max(2f, 2.0f * density));

            switch (kind) {
                case ICON_BACK: {
                    float r = s * 0.28f;
                    c.drawLine(cx + r, cy, cx - r, cy, paint);
                    c.drawLine(cx - r, cy, cx - r * 0.05f, cy - r * 0.85f, paint);
                    c.drawLine(cx - r, cy, cx - r * 0.05f, cy + r * 0.85f, paint);
                    break;
                }
                case ICON_REFRESH: {
                    float r = s * 0.28f;
                    RectF oval = new RectF(cx - r, cy - r, cx + r, cy + r);
                    c.drawArc(oval, -55, 290, false, paint);
                    float ax = cx + r * 0.65f;
                    float ay = cy - r * 0.72f;
                    float arm = r * 0.42f;
                    c.drawLine(ax, ay, ax, ay - arm * 0.85f, paint);
                    c.drawLine(ax, ay, ax - arm * 0.9f, ay + arm * 0.1f, paint);
                    break;
                }
                case ICON_CHEVRON: {
                    float r = s * 0.18f;
                    c.drawLine(cx - r * 0.4f, cy - r, cx + r * 0.6f, cy, paint);
                    c.drawLine(cx + r * 0.6f, cy, cx - r * 0.4f, cy + r, paint);
                    break;
                }
                case ICON_VULKAN: {
                    // Stylized chip + V glyph representing GPU / Vulkan driver
                    float r = s * 0.27f;
                    RectF box = new RectF(cx - r, cy - r, cx + r, cy + r);
                    c.drawRoundRect(box, r * 0.28f, r * 0.28f, paint);
                    float pin = r * 0.34f;
                    for (float off : new float[] {-r * 0.42f, r * 0.42f}) {
                        c.drawLine(cx + off, cy - r, cx + off, cy - r - pin, paint);
                        c.drawLine(cx + off, cy + r, cx + off, cy + r + pin, paint);
                        c.drawLine(cx - r, cy + off, cx - r - pin, cy + off, paint);
                        c.drawLine(cx + r, cy + off, cx + r + pin, cy + off, paint);
                    }
                    Path vPath = new Path();
                    vPath.moveTo(cx - r * 0.42f, cy - r * 0.34f);
                    vPath.lineTo(cx, cy + r * 0.42f);
                    vPath.lineTo(cx + r * 0.42f, cy - r * 0.34f);
                    c.drawPath(vPath, paint);
                    break;
                }
                case ICON_PERF: {
                    // Stylized speedometer gauge + needle representing Performance Mode
                    float r = s * 0.34f;
                    RectF arc = new RectF(cx - r, cy - r * 0.82f, cx + r, cy + r * 1.18f);
                    c.drawArc(arc, 155, 230, false, paint);
                    c.drawLine(cx, cy + r * 0.18f, cx + r * 0.52f, cy - r * 0.36f, paint);
                    paint.setStyle(Paint.Style.FILL);
                    c.drawCircle(cx, cy + r * 0.18f, Math.max(2.5f, 2.4f * density), paint);
                    paint.setStyle(Paint.Style.STROKE);
                    break;
                }
                case ICON_SEARCH: {
                    float r = s * 0.22f;
                    float ox = cx - r * 0.25f;
                    float oy = cy - r * 0.25f;
                    c.drawCircle(ox, oy, r, paint);
                    c.drawLine(ox + r * 0.72f, oy + r * 0.72f, cx + r * 1.2f, cy + r * 1.2f, paint);
                    break;
                }
                case ICON_CLEAR: {
                    float r = s * 0.22f;
                    c.drawLine(cx - r, cy - r, cx + r, cy + r, paint);
                    c.drawLine(cx + r, cy - r, cx - r, cy + r, paint);
                    break;
                }
                default:
                    break;
            }
        }
    }

    private static final class RadioIndicatorView extends View {
        private final boolean checked;
        private final int accent;
        private final int muted;
        private final float density;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        RadioIndicatorView(Context ctx, boolean checked, int accent, int muted, float density) {
            super(ctx);
            this.checked = checked;
            this.accent = accent;
            this.muted = muted;
            this.density = density;
        }

        @Override protected void onDraw(Canvas c) {
            float cx = getWidth() / 2f;
            float cy = getHeight() / 2f;
            float outer = Math.min(cx, cy) - 2f * density;
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(2f, 2f * density));
            paint.setColor(checked ? accent : muted);
            c.drawCircle(cx, cy, outer, paint);
            if (checked) {
                paint.setStyle(Paint.Style.FILL);
                c.drawCircle(cx, cy, outer * 0.52f, paint);
            }
        }
    }
}
