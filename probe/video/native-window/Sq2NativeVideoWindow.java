import android.os.Looper;
import android.view.Surface;
import android.view.SurfaceControl;

/** Finite native video overlay using the selected environment's existing display and codec services. */
public final class Sq2NativeVideoWindow {
    private static native int decode(Surface surface);
    private static native String codecLabel();

    @SuppressWarnings("deprecation")
    private static void run(String[] args) throws Exception {
        if ((args.length < 4 || args.length > 6) || !args[0].startsWith("/"))
            throw new IllegalArgumentException("ABSOLUTE_JNI_PATH DISPLAY_WIDTH DISPLAY_HEIGHT LAYER_STACK [SCENE]");
        int width = Integer.parseInt(args[1]);
        int height = Integer.parseInt(args[2]);
        int stack = Integer.parseInt(args[3]);
        String scene = args.length >= 5 ? args[4] : "baseline";
        if (!scene.equals("baseline") && !scene.equals("scale-up-left")
                && !scene.equals("scale-down-right") && !scene.equals("occlusion")
                && !scene.equals("fullscreen") && !scene.equals("fullscreen-mask"))
            throw new IllegalArgumentException("Unknown scene");
        boolean masked = scene.equals("fullscreen-mask");
        boolean fullScreen = scene.equals("fullscreen") || masked;
        int maskHeight = masked && args.length == 6 ? Integer.parseInt(args[5]) : 0;
        if ((masked && (maskHeight <= 0 || maskHeight > height)) || (!masked && args.length == 6))
            throw new IllegalArgumentException("Mask height is required only for fullscreen-mask");
        int videoWidth = fullScreen ? width : scene.equals("scale-up-left") ? 640
                : scene.equals("scale-down-right") ? 160 : 320;
        int videoHeight = fullScreen ? height : scene.equals("scale-up-left") ? 480
                : scene.equals("scale-down-right") ? 120 : 240;
        int videoX = scene.equals("scale-up-left") ? (width / 2 - videoWidth) / 2
                : scene.equals("scale-down-right") ? width / 2 + (width / 2 - videoWidth) / 2
                : (width - videoWidth) / 2;
        int videoY = (height - videoHeight) / 2;
        if (videoX < 0 || videoY < 0 || videoX + videoWidth > width
                || videoY + videoHeight > height)
            throw new IllegalArgumentException("Scene exceeds current display");
        if (width < 320 || height < 240 || width > 16384 || height > 16384 || stack < 0)
            throw new IllegalArgumentException("Invalid display dimensions/layer stack from SurfaceFlinger");
        if (Looper.myLooper() == null) Looper.prepareMainLooper();
        System.load(args[0]);
        String codec = codecLabel();
        SurfaceControl control = null;
        Surface surface = null;
        SurfaceControl cover = null;
        Surface coverSurface = null;
        try {
            control = new SurfaceControl.Builder().setName("SQ2 Native " + codec + " 30 frames" + (scene.equals("baseline") ? "" : " " + scene))
                    .setBufferSize(320, 240).setOpaque(true).setHidden(true).build();
            if (!control.isValid()) throw new IllegalStateException("Invalid SurfaceControl");
            surface = new Surface(control);
            if (!surface.isValid()) throw new IllegalStateException("Invalid Surface");
            try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                SurfaceControl.Transaction.class.getMethod("setLayerStack", SurfaceControl.class,
                        int.class).invoke(transaction, control, stack);
                transaction.setPosition(control, videoX, videoY)
                        .setScale(control, videoWidth / 320.0f, videoHeight / 240.0f)
                        .setLayer(control, 1000000).setVisibility(control, !masked).apply();
            }
            System.out.println("NATIVE_VIDEO_WINDOW_READY display=" + width + "x" + height
                    + " layer_stack=" + stack + " video=320x240 input_window=none");
            if (scene.equals("occlusion") || masked) {
                int coverWidth = masked ? width : 160;
                int coverHeight = masked ? maskHeight : 240;
                cover = new SurfaceControl.Builder().setName(masked ? "SQ2 temporary status mask" : "SQ2 opaque UI cover")
                        .setBufferSize(coverWidth, coverHeight).setOpaque(true).setHidden(true).build();
                coverSurface = new Surface(cover);
                android.graphics.Canvas canvas = coverSurface.lockCanvas(null);
                try {
                    canvas.drawColor(android.graphics.Color.BLACK);
                } finally {
                    coverSurface.unlockCanvasAndPost(canvas);
                }
                try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                    SurfaceControl.Transaction.class.getMethod("setLayerStack", SurfaceControl.class,
                            int.class).invoke(transaction, cover, stack);
                    transaction.setPosition(cover, masked ? 0 : videoX, masked ? 0 : videoY)
                            .setLayer(cover, masked ? 999999 : 1000001).setVisibility(cover, true).apply();
                }
            }
            if (masked) {
                System.out.println("NATIVE_MASK_BEFORE_READY height=" + maskHeight + " hold_ms=3000");
                System.out.flush();
                Thread.sleep(3000);
                try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                    transaction.setVisibility(control, true).apply();
                }
            }
            System.out.println("NATIVE_VIDEO_SCENE scene=" + scene + " destination="
                    + videoX + "," + videoY + "," + videoWidth + "," + videoHeight);
            int result = decode(surface);
            System.out.println("NATIVE_VIDEO_DECODE_RETURN result=" + result + " strict_pass=" + (result == 0));
            if (result == 0)
                System.out.println("NATIVE_VIDEO_SUBMISSIONS_COMPLETE pixels_verified=0 physical_scanout_verified=0");
            int holdMs = codec.equals("HEVC") ? 7000 : scene.equals("occlusion") ? 4500 : scene.equals("baseline") ? 3000 : 5000;
            System.out.println("NATIVE_VIDEO_HOLD_READY duration_ms=" + holdMs
                    + " capture_after_playback=1 decode_result=" + result);
            System.out.flush();
            if (!scene.equals("occlusion")) {
                Thread.sleep(holdMs);
            } else {
                String[] phases = {"covered", "uncovered", "covered-again"};
                int[] levels = {1000001, 999999, 1000001};
                for (int i = 0; i < phases.length; ++i) {
                    try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                        transaction.setLayer(cover, levels[i]).apply();
                    }
                    System.out.println("NATIVE_OCCLUSION_PHASE name=" + phases[i] + " hold_ms=1500");
                    System.out.flush();
                    Thread.sleep(1500);
                }
            }
            if (masked) {
                try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                    transaction.setVisibility(control, false).apply();
                }
                System.out.println("NATIVE_MASK_AFTER_READY height=" + maskHeight + " hold_ms=3000");
                System.out.flush();
                Thread.sleep(3000);
            }
            if (result != 0) throw new IllegalStateException("Native video diagnostic failed: " + result);
        } finally {
            try {
                if (cover != null) {
                    try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                        transaction.setVisibility(cover, false).apply();
                    } finally {
                        if (coverSurface != null) coverSurface.release();
                        cover.release();
                    }
                }
                if (control != null && control.isValid()) {
                    try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                        transaction.setVisibility(control, false).apply();
                    }
                }
            } finally {
                try {
                    if (surface != null) surface.release();
                } finally {
                    if (control != null) control.release();
                    System.out.println("NATIVE_VIDEO_SURFACE_RELEASED");
                }
            }
        }
    }

    public static void main(String[] args) {
        try {
            run(args);
            System.exit(0);
        } catch (Throwable failure) {
            System.err.println("NATIVE_VIDEO_WINDOW_FAILED " + failure);
            failure.printStackTrace();
            System.exit(1);
        }
    }
}
