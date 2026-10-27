// Reflection keeps this lab probe buildable with a stock JDK; run via app_process.
import java.lang.reflect.*;
import java.nio.file.*;

public final class Sq2AudioEffects {
    static Object call(Object target, String name, Class<?>[] types, Object... args) throws Exception {
        return target.getClass().getMethod(name, types).invoke(target, args);
    }
    public static void main(String[] args) throws Exception {
        try { run(args); }
        catch (InvocationTargetException e) {
            e.getCause().printStackTrace();
            System.exit(1);
        }
    }
    static void run(String[] args) throws Exception {
        String mode = args.length == 0 ? "query" : args[0];
        Class<?> audio = Class.forName("android.media.audiofx.AudioEffect");
        if (mode.equals("query")) {
            Object[] descriptors = (Object[])audio.getMethod("queryEffects").invoke(null);
            System.out.println("EFFECT_COUNT="+descriptors.length);
            for (Object d : descriptors) {
                Class<?> c = d.getClass();
                System.out.println(c.getField("name").get(d)+" type="+c.getField("type").get(d)+" uuid="+c.getField("uuid").get(d));
            }
            return;
        }
        if (!mode.equals("trial")) throw new IllegalArgumentException("query|trial");
        int seconds = args.length > 1 ? Integer.parseInt(args[1]) : 600;
        if (seconds < 1 || seconds > 600) throw new IllegalArgumentException("Trial duration: 1..600 seconds");
        Class<?> dp = Class.forName("android.media.audiofx.DynamicsProcessing");
        Class<?> cfg = Class.forName("android.media.audiofx.DynamicsProcessing$Config");
        Class<?> builder = Class.forName("android.media.audiofx.DynamicsProcessing$Config$Builder");
        Class<?> eq = Class.forName("android.media.audiofx.DynamicsProcessing$Eq");
        Class<?> band = Class.forName("android.media.audiofx.DynamicsProcessing$EqBand");
        int variant = dp.getField("VARIANT_FAVOR_FREQUENCY_RESOLUTION").getInt(null);
        Object b = builder.getConstructor(int.class,int.class,boolean.class,int.class,boolean.class,int.class,boolean.class,int.class,boolean.class)
            .newInstance(variant,2,true,6,false,0,false,0,false);
        Object e = eq.getConstructor(boolean.class,boolean.class,int.class).newInstance(true,true,6);
        float[] cutoffs = {80,125,200,300,1000,24000};
        float[] gains = {0,1.5f,3,1.5f,0,0};
        for (int i=0;i<cutoffs.length;i++) {
            Object eb = band.getConstructor(boolean.class,float.class,float.class).newInstance(true,cutoffs[i],gains[i]);
            call(e,"setBand",new Class<?>[]{int.class,band},i,eb);
        }
        call(b,"setPreEqAllChannelsTo",new Class<?>[]{eq},e);
        // Reserve the same 3 dB digitally that the trial supervisor compensates
        // at the codec, so full-scale media cannot clip solely from this EQ.
        call(b,"setInputGainAllChannelsTo",new Class<?>[]{float.class},-3.0f);
        Object config = call(b,"build",new Class<?>[]{});
        Object effect = dp.getConstructor(int.class,int.class,cfg).newInstance(0,0,config);
        try {
            if (!(Boolean)call(effect,"hasControl",new Class<?>[]{})) throw new IllegalStateException("No effect control");
            Object status=call(effect,"setEnabled",new Class<?>[]{boolean.class},true);
            if (((Integer)status)!=0) throw new IllegalStateException("setEnabled="+status);
            System.out.println("SQ2_BASS_EQ_READY enabled="+call(effect,"getEnabled",new Class<?>[]{}));
            System.out.println(call(effect,"getConfig",new Class<?>[]{}));
            System.out.flush();
            boolean enabled=true;
            Path control=Paths.get("/debug_ramdisk/sq2-bass-mode");
            for(int i=0;i<seconds;i++) {
                if(Files.exists(control)) {
                    String value=new String(Files.readAllBytes(control)).trim();
                    if(value.equals("stop")) break;
                    boolean next=!value.equals("off");
                    if(next!=enabled) {
                        int result=(Integer)call(effect,"setEnabled",new Class<?>[]{boolean.class},next);
                        if(result!=0) throw new IllegalStateException("setEnabled="+result);
                        enabled=next;
                        System.out.println("SQ2_BASS_EQ_ENABLED="+enabled);
                        System.out.flush();
                    }
                }
                Thread.sleep(1000);
            }
        } finally {
            call(effect,"setEnabled",new Class<?>[]{boolean.class},false);
            call(effect,"release",new Class<?>[]{});
            System.out.println("SQ2_BASS_EQ_RELEASED");
        }
    }
}
