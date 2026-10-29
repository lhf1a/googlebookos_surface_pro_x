package org.surface.sq2.videotest;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaCodecList;
import android.media.MediaCodecInfo;
import android.content.res.AssetFileDescriptor;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;

// Ordinary app UID, no device-node access and no privileged permissions.
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private volatile boolean cancelled;
    private Thread worker;
    private TextView status;
    private final AtomicInteger rendered = new AtomicInteger();
    private final Handler main = new Handler(Looper.getMainLooper());
    private synchronized void record(String message) {
        android.util.Log.i("SQ2VideoTest", message);
        try (FileOutputStream out = openFileOutput("result.txt", MODE_APPEND)) {
            out.write((message + "\n").getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch (Exception e) { android.util.Log.e("SQ2VideoTest", "log", e); }
        main.post(() -> status.setText(message));
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        deleteFile("result.txt");
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(LinearLayout.VERTICAL);
        status = new TextView(this); status.setTextSize(20); status.setPadding(20,20,20,20);
        layout.addView(status);
        SurfaceView video = new SurfaceView(this);
        layout.addView(video, new LinearLayout.LayoutParams(-1,0,1));
        video.getHolder().addCallback(this);setContentView(layout);
        record("SQ2 video: waiting for Surface");
    }
    @Override public void surfaceCreated(SurfaceHolder holder) {
        if (worker != null) return;
        worker = new Thread(() -> decode(holder), "sq2-player");worker.start();
    }
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height) {}
    @Override public void surfaceDestroyed(SurfaceHolder holder) { cancelled = true; }
    @Override public void onDestroy() { cancelled = true;super.onDestroy(); }
    private void decode(SurfaceHolder holder) {
        MediaCodec codec = null;MediaExtractor extractor = new MediaExtractor();boolean started=false;
        String name = getIntent().getStringExtra("codec");
        if (name == null) name = "c2.sq2.avc.experimental";
        try (AssetFileDescriptor file = getAssets().openFd("clip.mp4")) {
            extractor.setDataSource(file.getFileDescriptor(),file.getStartOffset(),file.getLength());
            extractor.selectTrack(0);MediaFormat format=extractor.getTrackFormat(0);
            String mime=format.getString(MediaFormat.KEY_MIME);
            String hardwareName;
            switch(mime) {
                case "video/hevc": hardwareName="c2.sq2.hevc.experimental"; break;
                case "video/avc": hardwareName="c2.sq2.avc.experimental"; break;
                case "video/x-vnd.on2.vp8": hardwareName="c2.sq2.vp8.experimental"; break;
                case "video/x-vnd.on2.vp9": hardwareName="c2.sq2.vp9.experimental"; break;
                default: throw new IllegalArgumentException("unsupported test MIME: "+mime);
            }
            int fps=format.getInteger(MediaFormat.KEY_FRAME_RATE);
            if(fps!=30&&fps!=60)throw new IllegalArgumentException("unsupported test frame rate");
            if("auto".equals(name)){
                MediaCodecList list=new MediaCodecList(MediaCodecList.ALL_CODECS);
                for(MediaCodecInfo candidate:list.getCodecInfos())if(candidate.getName().equals(hardwareName)){
                    MediaCodecInfo.CodecCapabilities caps=candidate.getCapabilitiesForType(mime);
                    record("CAPABILITY hardware="+candidate.isHardwareAccelerated()+" software="+candidate.isSoftwareOnly()+" profiles="+caps.profileLevels.length+" format_supported="+caps.isFormatSupported(format));
                }
                name=list.findDecoderForFormat(format);record("AUTO_SELECTED "+name);
                if(!hardwareName.equals(name))throw new IllegalStateException("automatic selection did not choose experimental hardware");
            }
            record("CREATE " + name + " uid=" + android.os.Process.myUid() + " format=" + format);
            if(getIntent().getBooleanExtra("probe-profile",false)){
                // Explicit lab probe before advertising a new profile. The SPS is unchanged.
                if(!"c2.sq2.avc.experimental".equals(name))throw new IllegalArgumentException("profile probe requires named hardware");
                format.removeKey(MediaFormat.KEY_PROFILE);format.removeKey(MediaFormat.KEY_LEVEL);
                record("PROFILE_PROBE using original bitstream without framework profile hint");
            }
            codec=MediaCodec.createByCodecName(name);
            codec.setOnFrameRenderedListener((c,pts,time) -> rendered.incrementAndGet(),main);
            codec.configure(format,holder.getSurface(),null,0);codec.start();started=true;
            record("START codec=" + codec.getName());
            int inputs=0,outputs=0,discarded=0;boolean inputEos=false,outputEos=false;
            boolean seekTest=getIntent().getBooleanExtra("seek",false),sought=false;
            int loops=getIntent().getIntExtra("loops",1),inputLoop=0;
            if((loops!=1&&loops!=6)||(seekTest&&loops!=1))throw new IllegalArgumentException("unsupported test loops");
            long begin=System.nanoTime(),deadline=begin+(loops*10L+15L)*1000000000L;
            long decodeEnd=begin;
            long timeline=begin;
            long seekStartUs=0;int expected=fps*10*loops;
            long previousPts=-1;
            MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
            while (!cancelled&&!outputEos&&System.nanoTime()<deadline) {
                if (!inputEos) {
                    int index=codec.dequeueInputBuffer(1000);
                    if(index>=0) {
                        ByteBuffer buffer=codec.getInputBuffer(index);
                        int bytes=extractor.readSampleData(buffer,0);
                        if(bytes<0&&inputLoop+1<loops){extractor.seekTo(0,MediaExtractor.SEEK_TO_PREVIOUS_SYNC);inputLoop++;bytes=extractor.readSampleData(buffer,0);record("INPUT_LOOP "+inputLoop);}
                        if(bytes<0) {codec.queueInputBuffer(index,0,0,0,MediaCodec.BUFFER_FLAG_END_OF_STREAM);inputEos=true;}
                        else {codec.queueInputBuffer(index,0,bytes,extractor.getSampleTime()+inputLoop*10000000L,0);inputs++;extractor.advance();}
                    }
                }
                int index=codec.dequeueOutputBuffer(info,1000);
                if(index>=0) {
                    if(info.size>0) {
                        if(info.presentationTimeUs<=previousPts)throw new IllegalStateException("non-increasing output PTS "+previousPts+" -> "+info.presentationTimeUs);
                        previousPts=info.presentationTimeUs;
                        if(sought&&info.presentationTimeUs<seekStartUs)throw new IllegalStateException("stale output after seek: "+info.presentationTimeUs);
                        long target=timeline+info.presentationTimeUs*1000;
                        // Bound render-ahead explicitly; different codec pools have different backpressure.
                        while(!cancelled&&target-System.nanoTime()>10000000L) Thread.sleep(2);
                        codec.releaseOutputBuffer(index,target);outputs++;
                    }
                    else codec.releaseOutputBuffer(index,false);
                    outputEos=(info.flags&MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0;
                    if(outputs>0&&outputs%30==0) record("PLAY inputs="+inputs+" outputs="+outputs+" rendered="+rendered.get()+" elapsed_ms="+(System.nanoTime()-begin)/1000000);
                    if(seekTest&&!sought&&outputs==60){
                        long wait=System.nanoTime()+2000000000L;
                        while(rendered.get()<outputs&&System.nanoTime()<wait)Thread.sleep(10);
                        if(rendered.get()!=60)throw new IllegalStateException("pre-seek render timeout");
                        discarded=inputs-outputs;record("FLUSH_BEGIN discarded="+discarded);
                        codec.flush();extractor.seekTo(5000000L,MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
                        seekStartUs=extractor.getSampleTime();
                        if(seekStartUs<4000000L||seekStartUs>5000000L||(extractor.getSampleFlags()&MediaExtractor.SAMPLE_FLAG_SYNC)==0)throw new IllegalStateException("invalid sync target "+seekStartUs);
                        expected=60+fps*10-(int)Math.round(seekStartUs*fps/1000000.0);
                        inputEos=false;outputEos=false;sought=true;timeline=System.nanoTime()-seekStartUs*1000;
                        previousPts=-1;
                        record("FLUSH_OK target_us="+seekStartUs+" expected_outputs="+expected);
                    }
                } else if(index==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) record("OUTPUT_FORMAT "+codec.getOutputFormat());
            }
            decodeEnd=System.nanoTime();
            long renderDeadline=System.nanoTime()+3000000000L;
            while(!cancelled&&rendered.get()<outputs&&System.nanoTime()<renderDeadline) Thread.sleep(20);
            long elapsedMs=(decodeEnd-begin)/1000000;
            boolean realtime=elapsedMs<=(seekTest?8000:loops*10000L+500);
            boolean good=!cancelled&&outputEos&&inputs==expected+discarded&&outputs==expected&&rendered.get()==expected&&realtime&&(!seekTest||sought);
            record("RESULT pass="+good+" inputs="+inputs+" outputs="+outputs+" rendered="+rendered.get()+" eos="+outputEos+" cancelled="+cancelled+" realtime="+realtime+" elapsed_ms="+elapsedMs);
            Thread.sleep(2000);
        } catch(Exception e) {record("ERROR "+e);}
        finally {
            if(codec!=null) {
                boolean clean=true;
                if(started) {
                    record("STOP_BEGIN");
                    try {codec.stop();record("STOP_OK");}
                    catch(Exception e){clean=false;record("STOP_ERROR "+errorDetail(e));}
                }
                record("RELEASE_BEGIN");
                try {codec.release();record(clean?"RELEASE_OK":"RELEASE_AFTER_STOP_ERROR");}
                catch(Exception e){record("RELEASE_ERROR "+errorDetail(e));}
            }
            extractor.release();
        }
    }
    private String errorDetail(Exception e) {
        if(e instanceof MediaCodec.CodecException) {
            MediaCodec.CodecException c=(MediaCodec.CodecException)e;
            return c+" code="+c.getErrorCode()+" diagnostic="+c.getDiagnosticInfo()+" recoverable="+c.isRecoverable()+" transient="+c.isTransient();
        }
        return android.util.Log.getStackTraceString(e);
    }
}
