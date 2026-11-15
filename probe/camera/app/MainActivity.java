package org.spx.camera.test;
import android.app.Activity;
import android.os.*;
import android.hardware.camera2.*;
import android.media.*;
import android.graphics.ImageFormat;
import android.graphics.BitmapFactory;
import android.util.Size;
import android.view.*;
import android.widget.*;
import android.util.Log;
import java.io.*;
import java.nio.ByteBuffer;
import java.util.*;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    String[] order={"1","0","1"};
    HandlerThread thread;
    Handler worker;
    SurfaceView preview;
    TextView status;
    CameraDevice camera;
    CameraCaptureSession session;
    ImageReader yuv,jpeg;
    int round,frames,targetFrames=40,switchDelayMs=500;
    boolean shot,stopped;
    boolean testYuv=true,nativePhoto,video,recording;
    int videoQuality=CamcorderProfile.QUALITY_720P;
    MediaRecorder recorder;
    File videoFile;
    int streamWidth=640,streamHeight=480,photoWidth,photoHeight;
    void note(String s) {
        Log.i("SpxCameraTest",s);
        runOnUiThread(()->status.setText(s));
        try(FileWriter w=new FileWriter(new File(getFilesDir(),"results.txt"),true)){w.write(s+"\n");}
        catch(IOException e){Log.e("SpxCameraTest","log",e);}
    }
    public void onCreate(Bundle b) {
        super.onCreate(b);
        String cameras=getIntent().getStringExtra("cameras");
        if(cameras!=null)order=cameras.split(",");
        testYuv=getIntent().getBooleanExtra("yuv",true);
        nativePhoto=getIntent().getBooleanExtra("nativePhoto",false);
        video=getIntent().getBooleanExtra("video",false);
        videoQuality=getIntent().getIntExtra("quality",CamcorderProfile.QUALITY_720P);
        streamWidth=getIntent().getIntExtra("width",640);
        streamHeight=getIntent().getIntExtra("height",480);
        targetFrames=Math.max(40,getIntent().getIntExtra("frames",40));
        switchDelayMs=Math.max(0,getIntent().getIntExtra("switchDelayMs",500));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout layout=new LinearLayout(this);layout.setOrientation(1);
        status=new TextView(this);layout.addView(status);
        preview=new SurfaceView(this);layout.addView(preview,new LinearLayout.LayoutParams(-1,0,1));
        setContentView(layout);
        thread=new HandlerThread("CameraTest");thread.start();worker=new Handler(thread.getLooper());
        preview.getHolder().setFixedSize(streamWidth,streamHeight);preview.getHolder().addCallback(this);
    }
    public void surfaceCreated(SurfaceHolder h){worker.post(this::open);}
    public void surfaceChanged(SurfaceHolder h,int f,int w,int t){}
    public void surfaceDestroyed(SurfaceHolder h){stopped=true;worker.post(this::release);}
    void release() {
        if(session!=null){session.close();session=null;}
        if(camera!=null){camera.close();camera=null;}
        if(recorder!=null){
            if(recording)try{recorder.stop();}catch(RuntimeException e){Log.e("SpxCameraTest","stop recorder",e);}
            recording=false;recorder.release();recorder=null;
        }
        if(yuv!=null){yuv.close();yuv=null;}
        if(jpeg!=null){jpeg.close();jpeg=null;}
    }
    void fail(String s){if(stopped)return;stopped=true;note("FAIL "+s);release();}
    void open() {
        if(stopped)return;
        if(round==order.length){note("PASS cameras="+Arrays.toString(order)+(video?" H264/AAC MP4":" preview"+(testYuv?"+YUV":"")+"+JPEG"));return;}
        frames=0;shot=false;
        CameraManager manager=getSystemService(CameraManager.class);
        try {
            note("OPEN "+order[round]+" frames="+targetFrames+" switchDelayMs="+switchDelayMs+" ids="+Arrays.toString(manager.getCameraIdList()));
            for(int quality:new int[]{CamcorderProfile.QUALITY_480P,CamcorderProfile.QUALITY_720P,CamcorderProfile.QUALITY_1080P}) {
                EncoderProfiles profiles=CamcorderProfile.getAll(order[round],quality);
                if(profiles!=null)for(EncoderProfiles.VideoProfile v:profiles.getVideoProfiles())
                    note("PROFILE camera="+order[round]+" quality="+quality+" codec="+v.getMediaType()+" "+v.getWidth()+"x"+v.getHeight()+" fps="+v.getFrameRate());
            }
            photoWidth=streamWidth;photoHeight=streamHeight;
            if(nativePhoto) {
                Size[] sizes=manager.getCameraCharacteristics(order[round]).get(
                    CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP).getOutputSizes(ImageFormat.JPEG);
                for(Size size:sizes)if((long)size.getWidth()*size.getHeight()>(long)photoWidth*photoHeight) {
                    photoWidth=size.getWidth();photoHeight=size.getHeight();
                }
            }
            note("STREAMS processed="+streamWidth+"x"+streamHeight+" JPEG="+photoWidth+"x"+photoHeight);
            manager.openCamera(order[round],new CameraDevice.StateCallback(){
                public void onOpened(CameraDevice c){camera=c;configure();}
                public void onDisconnected(CameraDevice c){fail("disconnected");}
                public void onError(CameraDevice c,int e){fail("device "+e);}
            },worker);
        }catch(Exception e){fail(e.toString());}
    }
    void configure() {
        if(video){configureVideo();return;}
        try {
            yuv=ImageReader.newInstance(streamWidth,streamHeight,ImageFormat.YUV_420_888,3);
            jpeg=ImageReader.newInstance(photoWidth,photoHeight,ImageFormat.JPEG,2);
            yuv.setOnImageAvailableListener(reader->{
                try(Image im=reader.acquireLatestImage()){
                    if(im==null)return;
                    if(im.getWidth()!=streamWidth || im.getHeight()!=streamHeight)throw new IOException("YUV dimensions");
                    ++frames;
                    if(frames==1 || frames==30){
                        ByteBuffer bytes=im.getPlanes()[0].getBuffer();long sum=0;
                        for(int i=0;i<bytes.limit();++i)sum+=bytes.get(i)&255;
                        note("YUV camera="+order[round]+" frame="+frames+" timestamp="+im.getTimestamp()+" sum="+sum);
                    }
                    if(frames>=targetFrames && !shot){shot=true;takePhoto();}
                }catch(Exception e){fail(e.toString());}
            },worker);
            jpeg.setOnImageAvailableListener(reader->{
                try(Image im=reader.acquireNextImage()){
                    if(im==null)return;
                    ByteBuffer data=im.getPlanes()[0].getBuffer();byte[] bytes=new byte[data.remaining()];data.get(bytes);
                    if(bytes.length<4 || (bytes[0]&255)!=255 || (bytes[1]&255)!=216)throw new IOException("bad JPEG");
                    BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;
                    BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);
                    if(options.outWidth!=photoWidth || options.outHeight!=photoHeight)throw new IOException("JPEG dimensions");
                    File file=new File(getFilesDir(),"camera-"+order[round]+"-round-"+round+".jpg");
                    try(FileOutputStream out=new FileOutputStream(file)){out.write(bytes);}
                    note("JPEG camera="+order[round]+" "+options.outWidth+"x"+options.outHeight+" bytes="+bytes.length);
                    worker.postDelayed(()->{release();round++;worker.postDelayed(this::open,switchDelayMs);},300);
                }catch(Exception e){fail(e.toString());}
            },worker);
            java.util.List<Surface> outputs=new ArrayList<>();
            outputs.add(preview.getHolder().getSurface());outputs.add(jpeg.getSurface());
            if(testYuv)outputs.add(yuv.getSurface());
            camera.createCaptureSession(outputs,
                new CameraCaptureSession.StateCallback(){
                    public void onConfigured(CameraCaptureSession s){
                        session=s;
                        try{
                            CaptureRequest.Builder b=camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                            b.addTarget(preview.getHolder().getSurface());if(testYuv)b.addTarget(yuv.getSurface());
                            s.setRepeatingRequest(b.build(),callback,worker);
                        }catch(Exception e){fail(e.toString());}
                    }
                    public void onConfigureFailed(CameraCaptureSession s){fail("configure");}
                },worker);
        }catch(Exception e){fail(e.toString());}
    }
    void configureVideo() {
        try {
            CamcorderProfile profile=CamcorderProfile.get(Integer.parseInt(order[round]),videoQuality);
            videoFile=new File(getFilesDir(),"video-"+order[round]+"-"+profile.videoFrameWidth+"x"+profile.videoFrameHeight+".mp4");
            recorder=new MediaRecorder(this);
            recorder.setAudioSource(MediaRecorder.AudioSource.MIC);
            recorder.setVideoSource(MediaRecorder.VideoSource.SURFACE);
            recorder.setProfile(profile);
            recorder.setOutputFile(videoFile.getAbsolutePath());
            recorder.setOrientationHint(180);
            recorder.prepare();
            Surface recordSurface=recorder.getSurface();
            camera.createCaptureSession(Arrays.asList(preview.getHolder().getSurface(),recordSurface),
                new CameraCaptureSession.StateCallback(){
                    public void onConfigured(CameraCaptureSession s){
                        session=s;
                        try {
                            CaptureRequest.Builder request=camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
                            request.addTarget(preview.getHolder().getSurface());request.addTarget(recordSurface);
                            session.setRepeatingRequest(request.build(),null,worker);
                            recorder.start();recording=true;
                            note("RECORDING camera="+order[round]+" "+profile.videoFrameWidth+"x"+profile.videoFrameHeight);
                            worker.postDelayed(()->finishVideo(profile),5000);
                        }catch(Exception e){fail(e.toString());}
                    }
                    public void onConfigureFailed(CameraCaptureSession s){fail("video configure");}
                },worker);
        }catch(Exception e){fail(e.toString());}
    }
    void finishVideo(CamcorderProfile profile) {
        if(stopped)return;
        try {
            recorder.stop();recording=false;
            try(MediaMetadataRetriever media=new MediaMetadataRetriever()) {
                media.setDataSource(videoFile.getAbsolutePath());
                int w=Integer.parseInt(media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH));
                int h=Integer.parseInt(media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT));
                long duration=Long.parseLong(media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION));
                String audio=media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO);
                if(w!=profile.videoFrameWidth || h!=profile.videoFrameHeight || duration<1000 || !"yes".equals(audio))
                    throw new IOException("invalid saved MP4");
                note("VIDEO camera="+order[round]+" "+w+"x"+h+" durationMs="+duration+" audio="+audio+" bytes="+videoFile.length());
            }
            release();round++;worker.postDelayed(this::open,switchDelayMs);
        }catch(Exception e){fail(e.toString());}
    }
    CameraCaptureSession.CaptureCallback callback=new CameraCaptureSession.CaptureCallback(){
        public void onCaptureCompleted(CameraCaptureSession s,CaptureRequest r,TotalCaptureResult result){
            if(!testYuv && !shot){
                ++frames;
                if(frames==1 || frames==30)note("PREVIEW camera="+order[round]+" frame="+frames+
                    " timestamp="+result.get(CaptureResult.SENSOR_TIMESTAMP));
                if(frames>=targetFrames){shot=true;takePhoto();}
            }
        }
        public void onCaptureFailed(CameraCaptureSession s,CaptureRequest r,CaptureFailure f){
            fail("capture frame="+f.getFrameNumber()+" reason="+f.getReason());
        }
    };
    void takePhoto() {
        try{
            CaptureRequest.Builder b=camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
            b.addTarget(jpeg.getSurface());b.set(CaptureRequest.JPEG_ORIENTATION,180);
            session.capture(b.build(),callback,worker);
        }catch(Exception e){fail(e.toString());}
    }
}
