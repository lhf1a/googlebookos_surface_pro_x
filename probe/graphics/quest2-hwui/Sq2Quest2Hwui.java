import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.HardwareRenderer;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RecordingCanvas;
import android.graphics.Rect;
import android.graphics.RenderNode;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.HardwareBuffer;
import android.media.Image;
import android.media.ImageReader;
import android.os.Looper;
import android.os.SharedMemory;
import android.os.SystemClock;

import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;

/** Finite actual HWUI producer test. The ImageReader bypasses SurfaceFlinger:
 * resulting pixels distinguish a HWUI/driver fault from desktop composition.
 * A supervisor must impose an external timeout; namespaces do not isolate GPU hangs.
 */
public final class Sq2Quest2Hwui {
    private static final int WIDTH = 640, HEIGHT = 360, BACKGROUND = 0xff234567;

    private static void initializeStandaloneFonts() throws Exception {
        // Actual cached Android 17 Typeface.<clinit> creates placeholders and
        // preloads files, but does not install the system font map. app_process
        // bypasses the ordinary application bootstrap. Its documented-in-DEX
        // setSystemFontMap(SharedMemory=null) path loads the preinstalled map,
        // resolves placeholders and calls nativeSetDefault. Do that once only
        // when the map is empty, before the first Canvas/Paint text operation.
        java.lang.reflect.Method mapMethod = Typeface.class.getDeclaredMethod("getSystemFontMap");
        mapMethod.setAccessible(true);
        Map<?, ?> before = (Map<?, ?>) mapMethod.invoke(null);
        if (before.isEmpty()) {
            java.lang.reflect.Method initialize = Typeface.class.getDeclaredMethod(
                    "setSystemFontMap", SharedMemory.class);
            initialize.setAccessible(true);
            initialize.invoke(null, new Object[] {null});
        }
        Map<?, ?> after = (Map<?, ?>) mapMethod.invoke(null);
        java.lang.reflect.Method getDefault = Typeface.class.getDeclaredMethod("getDefault");
        getDefault.setAccessible(true);
        Object defaultTypeface = getDefault.invoke(null);
        if (after.isEmpty() || defaultTypeface == null)
            throw new IllegalStateException("Standalone system font map unavailable");
        java.lang.reflect.Method nativeInstance = Typeface.class.getDeclaredMethod("getNativeInstance");
        nativeInstance.setAccessible(true);
        if ((Long) nativeInstance.invoke(defaultTypeface) == 0)
            throw new IllegalStateException("Standalone default typeface has no native instance");
        System.out.println("HWUI_FONTS map_entries=" + after.size() + " default_initialized=1");
    }

    private static void checkEnvironment(boolean mesa) throws Exception {
        String own = Files.readSymbolicLink(Paths.get("/proc/self/ns/mnt")).toString();
        String init = Files.readSymbolicLink(Paths.get("/proc/1/ns/mnt")).toString();
        if (!mesa && own.equals(init)) throw new IllegalStateException("Private EGL namespace required");
        String guard = new String(Files.readAllBytes(Paths.get(
                "/sys/module/suspend/parameters/screen_only")), StandardCharsets.US_ASCII).trim();
        if (!guard.equals("Y")) throw new IllegalStateException("screen_only guard is not Y");
        for (String key : new String[] {"LD_PRELOAD", "LD_LIBRARY_PATH"}) {
            String value = System.getenv(key);
            if (value != null && !value.isEmpty()) throw new IllegalStateException("Unset " + key);
        }
        System.out.println("HWUI_ENV expected=" + (mesa ? "mesa" : "quest2") + " namespace=" + own);
    }

    private static Bitmap icon(int phase) {
        int[] pixels = new int[64 * 64];
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            int r = (x * 7 + y * 3 + phase * 29) & 255;
            int g = (x * 2 + y * 11 + phase * 37) & 255;
            int b = (x * 13 + y * 5 + phase * 43) & 255;
            pixels[y * 64 + x] = Color.rgb(r, g, b);
        }
        Bitmap result = Bitmap.createBitmap(pixels, 64, 64, Bitmap.Config.ARGB_8888);
        result.setDensity(Bitmap.DENSITY_NONE);
        return result;
    }

    private static void scene(Canvas canvas, Bitmap software, Bitmap hardware, int phase) {
        canvas.drawColor(BACKGROUND);
        Paint paint = new Paint();
        paint.setFilterBitmap(false);
        paint.setAntiAlias(false);
        canvas.drawBitmap(software, 16, 16, paint);
        canvas.drawBitmap(hardware, 96, 16, paint);
        canvas.drawBitmap(software, null, new Rect(176, 16, 304, 144), paint);
        paint.setAlpha(128);
        canvas.drawBitmap(software, 320, 16, paint);
        paint.setAlpha(255);
        paint.setColor(phase % 2 == 0 ? 0xff20c040 : 0xffd04080);
        canvas.drawRect(400, 16, 480, 80, paint);
        Path path = new Path();
        path.moveTo(496, 80); path.lineTo(528, 16); path.lineTo(560, 80); path.close();
        canvas.drawPath(path, paint);
        paint.setAntiAlias(true);
        paint.setColor(0x80ff8020);
        canvas.drawRoundRect(400, 100, 560, 220, 24, 24, paint);
        paint.setColor(Color.WHITE);
        canvas.drawCircle(480, 160, 24, paint);
        paint.setShader(new LinearGradient(16, 168, 304, 168,
                0xffff2000, 0xff0020ff, Shader.TileMode.CLAMP));
        paint.setAntiAlias(false);
        canvas.drawRect(16, 168, 304, 216, paint);
        paint.setShader(null);
        paint.setColor(Color.WHITE);
        paint.setAntiAlias(true);
        paint.setTextSize(28);
        canvas.drawText("HWUI Bitmap / Path / Alpha " + phase, 16, 280, paint);
    }

    private static void save(Bitmap bitmap, java.nio.file.Path path) throws Exception {
        if (Files.exists(path)) throw new IllegalStateException("Refusing existing PNG " + path);
        try (FileOutputStream stream = new FileOutputStream(path.toFile())) {
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream))
                throw new IllegalStateException("PNG encode failed");
            stream.getFD().sync();
        }
    }

    private static Bitmap pixels(Image image) {
        if (image.getWidth() != WIDTH || image.getHeight() != HEIGHT ||
                image.getFormat() != PixelFormat.RGBA_8888 || image.getPlanes().length != 1 ||
                !image.getCropRect().equals(new Rect(0, 0, WIDTH, HEIGHT)))
            throw new IllegalStateException("Unexpected ImageReader layout");
        Image.Plane plane = image.getPlanes()[0];
        int stride = plane.getRowStride(), step = plane.getPixelStride();
        ByteBuffer buffer = plane.getBuffer();
        if (step != 4 || stride < WIDTH * step ||
                buffer.remaining() < (HEIGHT - 1) * stride + WIDTH * step)
            throw new IllegalStateException("Unexpected RGBA plane bounds");
        int origin = buffer.position();
        int[] data = new int[WIDTH * HEIGHT];
        for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++) {
            int offset = origin + y * stride + x * step;
            data[y * WIDTH + x] = Color.argb(buffer.get(offset + 3) & 255,
                    buffer.get(offset) & 255, buffer.get(offset + 1) & 255, buffer.get(offset + 2) & 255);
        }
        System.out.println("HWUI_IMAGE row_stride=" + stride + " pixel_stride=" + step);
        return Bitmap.createBitmap(data, WIDTH, HEIGHT, Bitmap.Config.ARGB_8888);
    }

    private static int distance(int a, int b) {
        return Math.max(Math.abs(Color.alpha(a) - Color.alpha(b)),
                Math.max(Math.abs(Color.red(a) - Color.red(b)),
                Math.max(Math.abs(Color.green(a) - Color.green(b)), Math.abs(Color.blue(a) - Color.blue(b)))));
    }

    private static boolean region(Bitmap actual, Bitmap expected, String name,
            int left, int top, int right, int bottom, int tolerance) {
        int bad = 0, max = 0;
        for (int y = top; y < bottom; y++) for (int x = left; x < right; x++) {
            int delta = distance(actual.getPixel(x, y), expected.getPixel(x, y));
            max = Math.max(max, delta);
            if (delta > tolerance) {
                if (bad < 3) System.out.printf("HWUI_BAD region=%s x=%d y=%d actual=%08x expected=%08x%n",
                        name, x, y, actual.getPixel(x, y), expected.getPixel(x, y));
                bad++;
            }
        }
        System.out.println("HWUI_REGION name=" + name + " checked=" + ((right-left)*(bottom-top))
                + " tolerance=" + tolerance + " bad=" + bad + " max_delta=" + max);
        return bad == 0;
    }

    private static boolean verify(Bitmap actual, Bitmap expected) {
        boolean good = true;
        good &= region(actual, expected, "cpu-icon", 16, 16, 80, 80, 0);
        good &= region(actual, expected, "hardware-icon", 96, 16, 160, 80, 0);
        good &= region(actual, expected, "scaled-icon", 176, 16, 304, 144, 0);
        good &= region(actual, expected, "alpha-icon", 320, 16, 384, 80, 1);
        good &= region(actual, expected, "solid-rect", 404, 20, 476, 76, 0);
        good &= region(actual, expected, "linear-gradient", 20, 172, 300, 212, 2);
        good &= region(actual, expected, "background", 16, 310, 624, 344, 0);
        // Font rasterizers may differ at antialiased edges. Count expected
        // strong ink and require most of those pixels to remain recognisable;
        // retain both PNGs rather than labelling the whole image bit-exact.
        int ink = 0, matched = 0;
        for (int y = 240; y < 284; y++) for (int x = 16; x < 620; x++) {
            if (distance(expected.getPixel(x, y), BACKGROUND) > 128) {
                ink++;
                if (distance(actual.getPixel(x, y), expected.getPixel(x, y)) <= 48) matched++;
            }
        }
        System.out.println("HWUI_TEXT strong_ink=" + ink + " matched=" + matched + " required_percent=85");
        return good && ink > 100 && matched * 100 >= ink * 85;
    }

    private static void verifyLibraries(boolean mesa, java.nio.file.Path output) throws Exception {
        List<String> lines = Files.readAllLines(Paths.get("/proc/self/maps"), StandardCharsets.US_ASCII);
        boolean closed = false, freedreno = false, hwui = false;
        StringBuilder selected = new StringBuilder();
        for (String line : lines) {
            if (line.contains("libGLESv2_adreno.so")) closed = true;
            if (line.contains("libEGL_freedreno.so")) freedreno = true;
            if (line.contains("/libhwui.so")) hwui = true;
            if (line.contains("libEGL") || line.contains("libGLES") || line.contains("libhwui") ||
                    line.contains("libq2v77") || line.contains("libllvm")) selected.append(line).append('\n');
        }
        Files.write(output.resolve("loaded-libraries.txt"), selected.toString().getBytes(StandardCharsets.US_ASCII));
        if (!hwui || (mesa ? (!freedreno || closed) : !closed))
            throw new IllegalStateException("Unexpected actual renderer libraries");
        System.out.println("HWUI_LIBRARIES hwui=" + hwui + " closed=" + closed + " freedreno=" + freedreno);
    }

    @SuppressWarnings("deprecation")
    private static void run(String[] args) throws Exception {
        if (args.length != 0) throw new IllegalArgumentException("Use SQ2_HWUI_OUTPUT and SQ2_HWUI_MESA");
        boolean mesa = "1".equals(System.getenv("SQ2_HWUI_MESA"));
        checkEnvironment(mesa);
        String name = System.getenv("SQ2_HWUI_OUTPUT");
        if (name == null || !name.matches("/data/local/tmp/sq2-closed/hwui-evidence/[A-Za-z0-9_-]+"))
            throw new IllegalArgumentException("SQ2_HWUI_OUTPUT must be a fresh hwui-evidence child");
        java.nio.file.Path output = Paths.get(name);
        Files.createDirectories(output.getParent());
        Files.createDirectory(output);
        if (Looper.myLooper() == null) Looper.prepareMainLooper();
        initializeStandaloneFonts();
        if (!HardwareRenderer.isDrawingEnabled()) throw new IllegalStateException("HWUI drawing disabled");
        HardwareRenderer renderer = new HardwareRenderer();
        RenderNode node = new RenderNode("SQ2 hardware bitmap diagnostics");
        node.setPosition(0, 0, WIDTH, HEIGHT);
        boolean pass = true;
        try (ImageReader reader = ImageReader.newInstance(WIDTH, HEIGHT, PixelFormat.RGBA_8888, 3,
                HardwareBuffer.USAGE_GPU_COLOR_OUTPUT | HardwareBuffer.USAGE_CPU_READ_OFTEN)) {
            renderer.setName("SQ2 HWUI isolated");
            renderer.setOpaque(true);
            renderer.setSurface(reader.getSurface());
            renderer.setContentRoot(node);
            for (int phase = 0; phase < 3; phase++) {
                Bitmap software = icon(phase);
                Bitmap hardware = software.copy(Bitmap.Config.HARDWARE, false);
                if (hardware == null || hardware.getConfig() != Bitmap.Config.HARDWARE)
                    throw new IllegalStateException("Hardware bitmap copy failed");
                try (HardwareBuffer buffer = hardware.getHardwareBuffer()) {
                    System.out.println("HWUI_HARDWARE_BUFFER phase=" + phase + " width=" + buffer.getWidth()
                            + " height=" + buffer.getHeight() + " format=" + buffer.getFormat()
                            + " layers=" + buffer.getLayers() + " usage=0x" + Long.toHexString(buffer.getUsage()));
                }
                if ("1".equals(System.getenv("SQ2_HWUI_READBACK"))) {
                    // Diagnostic only: Android's hardware->software copy may
                    // itself use GPU readback and synchronize the producer.
                    // It is not a direct DMA-buffer memory inspection. Keep
                    // the normal scene drawing and its independent verdict.
                    Bitmap readback = hardware.copy(Bitmap.Config.ARGB_8888, false);
                    if (readback == null || readback.getConfig() != Bitmap.Config.ARGB_8888) {
                        System.out.println("HWUI_HARDWARE_READBACK phase=" + phase + " status=NULL_OR_WRONG_CONFIG");
                        pass = false;
                    } else {
                        save(software, output.resolve("icon-source-" + phase + ".png"));
                        save(readback, output.resolve("icon-readback-" + phase + ".png"));
                        boolean copied = region(readback, software, "hardware-roundtrip", 0, 0, 64, 64, 0);
                        System.out.println("HWUI_HARDWARE_READBACK phase=" + phase + " status=" + (copied ? "PASS" : "FAIL"));
                        pass &= copied;
                        readback.recycle();
                    }
                }
                Bitmap reference = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888);
                reference.setDensity(Bitmap.DENSITY_NONE);
                Canvas cpu = new Canvas(reference);
                scene(cpu, software, software, phase);
                RecordingCanvas gpu = node.beginRecording(WIDTH, HEIGHT);
                if (!gpu.isHardwareAccelerated()) throw new IllegalStateException("Software recording canvas");
                scene(gpu, software, hardware, phase);
                node.endRecording();
                int sync = renderer.createRenderRequest().setVsyncTime(System.nanoTime())
                        .setWaitForPresent(true).syncAndDraw();
                System.out.println("HWUI_DRAW phase=" + phase + " sync=" + sync);
                if (sync != HardwareRenderer.SYNC_OK && sync != HardwareRenderer.SYNC_REDRAW_REQUESTED)
                    throw new IllegalStateException("HWUI sync failed: " + sync);
                Image image = null;
                long deadline = SystemClock.uptimeMillis() + 5000;
                while (image == null && SystemClock.uptimeMillis() < deadline) {
                    image = reader.acquireNextImage();
                    if (image == null) Thread.sleep(10);
                }
                if (image == null) throw new IllegalStateException("ImageReader timeout");
                try {
                    Bitmap actual = pixels(image);
                    save(actual, output.resolve("actual-" + phase + ".png"));
                    save(reference, output.resolve("reference-" + phase + ".png"));
                    pass &= verify(actual, reference);
                    actual.recycle();
                } finally { image.close(); }
                node.discardDisplayList();
                hardware.recycle(); software.recycle(); reference.recycle();
            }
            verifyLibraries(mesa, output);
        } finally {
            renderer.destroy();
            node.discardDisplayList();
        }
        System.out.println("HWUI_RESULT frames=3 expected=" + (mesa ? "mesa" : "quest2")
                + " status=" + (pass ? "PASS" : "FAIL") + " output=" + name);
        if (!pass) throw new IllegalStateException("HWUI pixel comparison failed; inspect retained PNGs");
    }

    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) {
            System.err.println("HWUI_PROBE_FAIL " + failure);
            failure.printStackTrace(); System.exit(1);
        }
    }
}
