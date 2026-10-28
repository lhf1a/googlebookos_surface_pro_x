package org.surface.sq2.mictest;

import android.app.Activity;
import android.os.Bundle;
import android.media.*;
import android.widget.*;
import android.util.Log;
import java.io.*;

/** Foreground, explicitly started ten-second capture through the public AudioRecord API. */
public class MainActivity extends Activity {
    TextView status;
    volatile boolean busy, cancelled;
    short[] recorded;
    int recordedChannels, recordedRate;
    void show(String text) { Log.i("SQ2MicTest", text); runOnUiThread(() -> status.setText(text)); }
    public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(1); layout.setPadding(36,60,36,24);
        status = new TextView(this); status.setTextSize(20); layout.addView(status);
        button(layout, "录音 10 秒（48 kHz 双声道）", () -> record(48000, 2));
        button(layout, "录音 10 秒（16 kHz 单声道）", () -> record(16000, 1));
        button(layout, "回放刚才录音", this::play);
        button(layout, "查看输入设备", this::devices);
        setContentView(layout); devices();
        if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{android.Manifest.permission.RECORD_AUDIO}, 1);
    }
    void button(LinearLayout layout, String title, Runnable action) {
        Button b = new Button(this); b.setText(title); b.setOnClickListener(v -> action.run()); layout.addView(b);
    }
    void devices() {
        StringBuilder text = new StringBuilder("只在点击录音后采集，离开此页面停止。\n录音保存在本应用本地目录。\n");
        for (AudioDeviceInfo d: getSystemService(AudioManager.class).getDevices(AudioManager.GET_DEVICES_INPUTS))
            text.append("输入: ").append(d.getProductName()).append(" type=").append(d.getType()).append(" id=").append(d.getId()).append("\n");
        show(text.toString());
    }
    void record(int rate, int channels) {
        if (busy) return;
        busy = true; cancelled = false; recorded = null;
        new Thread(() -> {
            AudioRecord mic = null;
            try {
                int mask = channels == 2 ? AudioFormat.CHANNEL_IN_STEREO : AudioFormat.CHANNEL_IN_MONO;
                int min = AudioRecord.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_16BIT);
                if (min < 0) throw new IOException("minimum buffer error=" + min);
                mic = new AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.MIC)
                    .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate).setChannelMask(mask).build())
                    .setBufferSizeInBytes(Math.max(min*4, rate*channels/5*2)).build();
                if (mic.getState() != AudioRecord.STATE_INITIALIZED) throw new IOException("not initialized");
                short[] data = new short[rate*channels*10]; int offset = 0;
                long start = System.nanoTime(); mic.startRecording();
                while (offset < data.length && !cancelled && System.nanoTime()-start < 15_000_000_000L) {
                    int n = mic.read(data, offset, Math.min(rate*channels/10, data.length-offset), AudioRecord.READ_NON_BLOCKING);
                    if (n < 0) throw new IOException("read=" + n);
                    if (n == 0) { Thread.sleep(5); continue; }
                    offset += n;
                    if (offset % (rate*channels) < n) show("正在录音 " + offset/(rate*channels) + "/10 秒，请讲话。");
                }
                double elapsed = (System.nanoTime()-start)/1e9;
                mic.stop();
                if (cancelled || offset != data.length) throw new IOException("capture incomplete: " + offset);
                recorded = data; recordedChannels = channels; recordedRate = rate;
                try (DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(getFilesDir(), "mic-"+rate+"-"+channels+".pcm"))))) {
                    for (short v: data) { out.writeByte(v & 255); out.writeByte((v >>> 8) & 255); }
                }
                double squares = 0; int peak = 0, zero = 0, clipped = 0;
                for (short v: data) { squares += (double)v*v; peak=Math.max(peak,Math.abs((int)v)); if(v==0)zero++; if(Math.abs((int)v)>=32767)clipped++; }
                show(String.format(java.util.Locale.ROOT,"录音完成：%d Hz / %d 声道\n耗时 %.2f 秒，RMS %.1f dBFS\n峰值 %d，全零样本 %d/%d，削波 %d\n点击回放，确认人声和速度。",rate,channels,elapsed,20*Math.log10(Math.max(1e-10,Math.sqrt(squares/data.length))/32768),peak,zero,data.length,clipped));
            } catch (Exception e) { show("录音失败："+e); Log.e("SQ2MicTest","capture",e); }
            finally { if(mic!=null)mic.release(); busy=false; }
        }, "SQ2MicCapture").start();
    }
    void play() {
        if(busy || recorded==null)return;
        busy=true; cancelled=false;
        new Thread(() -> {
            AudioTrack track=null;
            try {
                int mask=recordedChannels==2?AudioFormat.CHANNEL_OUT_STEREO:AudioFormat.CHANNEL_OUT_MONO;
                track=new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                    .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(recordedRate).setChannelMask(mask).build())
                    .setBufferSizeInBytes(recorded.length*2).setTransferMode(AudioTrack.MODE_STATIC).build();
                int n=track.write(recorded,0,recorded.length); if(n!=recorded.length)throw new IOException("write="+n);
                show("正在回放原始录音，未额外放大。"); track.play();
                long end=System.nanoTime()+12_000_000_000L;
                while(!cancelled && track.getPlaybackHeadPosition()<recorded.length/recordedChannels && System.nanoTime()<end)Thread.sleep(50);
                show("回放结束。可重录或返回诊断应用验证。");
            } catch(Exception e) { show("回放失败："+e); }
            finally { if(track!=null)track.release(); busy=false; }
        },"SQ2MicPlayback").start();
    }
    protected void onPause(){ cancelled=true; super.onPause(); }
}
