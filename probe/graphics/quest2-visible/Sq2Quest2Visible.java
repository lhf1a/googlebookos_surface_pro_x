import android.graphics.PixelFormat;
import android.graphics.Point;
import android.os.Looper;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceControl;

import java.lang.reflect.InvocationTargetException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

/** A finite lab overlay. Launch only after entering the private donor EGL namespace.
 * Do not use run-android-java after unshare: its nsenter would restore PID 1's mounts.
 * The external supervisor must retain a timeout; a shared GPU hang can block EGL.
 */
public final class Sq2Quest2Visible {
    private static native int draw(Surface surface, int width, int height);

    private static int extent(String requested, int fallback, int available) {
        int value = requested == null ? Math.min(fallback, available) : Integer.parseInt(requested);
        if (value < 64 || value > available)
            throw new IllegalArgumentException("Overlay dimension outside visible display: " + value);
        return value;
    }

    private static void checkPrivateEnvironment() throws Exception {
        String own = Files.readSymbolicLink(Paths.get("/proc/self/ns/mnt")).toString();
        String init = Files.readSymbolicLink(Paths.get("/proc/1/ns/mnt")).toString();
        if (own.equals(init)) throw new IllegalStateException("Private EGL mount namespace required");
        for (String key : new String[] {"LD_PRELOAD", "LD_LIBRARY_PATH"}) {
            String value = System.getenv(key);
            if (value != null && !value.isEmpty()) {
                throw new IllegalStateException("Unset " + key + " for system EGL dispatch");
            }
        }
        System.out.println("VISIBLE_NAMESPACE=" + own + " INIT_NAMESPACE=" + init);
    }

    @SuppressWarnings("deprecation")
    private static void run(String[] args) throws Exception {
        if (args.length > 1) throw new IllegalArgumentException("[absolute JNI library path]");
        String library = args.length == 1 ? args[0]
                : "/data/local/tmp/sq2-closed/visible/libquest2-visible.so";
        if (!library.startsWith("/")) throw new IllegalArgumentException("JNI path must be absolute");
        checkPrivateEnvironment();
        if (Looper.myLooper() == null) Looper.prepareMainLooper();

        // These hidden signatures were checked in the actual cached framework.jar.
        Class<?> dm = Class.forName("android.hardware.display.DisplayManagerGlobal");
        Object manager = dm.getMethod("getInstance").invoke(null);
        Display display = (Display) dm.getMethod("getRealDisplay", int.class).invoke(manager, 0);
        if (display == null) throw new IllegalStateException("Default display is unavailable");
        int stack = (Integer) Display.class.getMethod("getLayerStack").invoke(display);
        Point size = new Point();
        display.getRealSize(size);
        final int inset = 64;
        int width = extent(System.getenv("SQ2_VISIBLE_WIDTH"), 640, size.x - 2 * inset);
        int height = extent(System.getenv("SQ2_VISIBLE_HEIGHT"), 360, size.y - 2 * inset);
        System.out.println("VISIBLE_DISPLAY id=" + display.getDisplayId() + " layer_stack=" + stack
                + " size=" + size.x + "x" + size.y + " rotation=" + display.getRotation()
                + " overlay=" + width + "x" + height + " position=" + inset + "," + inset);

        SurfaceControl control = null;
        Surface surface = null;
        try {
            String producer = System.getenv("SQ2_VISIBLE_PRODUCER");
            boolean hwui = "hwui".equals(producer);
            if (producer != null && !hwui && !"gles".equals(producer))
                throw new IllegalArgumentException("SQ2_VISIBLE_PRODUCER must be gles or hwui");
            if (!hwui) System.load(library);
            control = new SurfaceControl.Builder().setName("SQ2 Quest2 private EGL 5s")
                    .setBufferSize(width, height).setFormat(PixelFormat.RGBA_8888)
                    .setOpaque(true).setHidden(true).build();
            if (!control.isValid()) throw new IllegalStateException("Invalid SurfaceControl");
            surface = new Surface(control);
            if (!surface.isValid()) throw new IllegalStateException("Invalid Surface");
            try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                SurfaceControl.Transaction.class.getMethod("setLayerStack", SurfaceControl.class,
                        int.class).invoke(transaction, control, stack);
                transaction.setPosition(control, inset, inset).setLayer(control, 1000000)
                        .setVisibility(control, true).apply();
            }
            System.out.println("VISIBLE_OVERLAY_SHOWN duration_ms=5000 input_window=none");
            int frames = hwui ? Sq2Quest2Hwui.drawVisible(surface, width, height)
                    : draw(surface, width, height);
            System.out.println("VISIBLE_SUBMISSIONS_COMPLETE frames=" + frames
                    + " visual_confirmation_required=1");
        } finally {
            if (control != null && control.isValid()) {
                try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                    transaction.setVisibility(control, false).apply();
                } catch (Throwable failure) {
                    System.err.println("VISIBLE_HIDE_FAILURE " + failure);
                }
            }
            try {
                if (surface != null) surface.release();
            } finally {
                try {
                    if (control != null) control.release();
                } finally {
                    System.out.println("VISIBLE_OVERLAY_RELEASED");
                }
            }
        }
    }

    public static void main(String[] args) {
        try {
            run(args);
            System.out.println("VISIBLE_PROBE_COMPLETE visual_confirmation_required=1");
            // app_process starts Binder threads; all overlay/EGL cleanup has completed.
            System.exit(0);
        } catch (Throwable failure) {
            if (failure instanceof InvocationTargetException && failure.getCause() != null) {
                failure = failure.getCause();
            }
            System.err.println("VISIBLE_PROBE_FAIL " + failure);
            failure.printStackTrace();
            System.exit(1);
        }
    }
}
