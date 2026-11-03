package org.surface.touchcalibration;
import android.app.Activity;
import android.content.Intent;
import org.json.*;
import android.os.*;
import android.graphics.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

public class MainActivity extends Activity {
  File dir;String device="",nonce="",pending="",noiseNonce="",patch="",exportNotice="";
  Calibration original,candidate;final Handler handler=new Handler();final WizardFlow flow=new WizardFlow();
  boolean leaving=false,waiting=false,preview=false,noisePreview=false,collecting=false,noiseCapturing=false;
  boolean idleArmed=false,retesting=false,noiseBand=false,patchAvailable=false,exporting=false,thumbAfter=false;
  long deadline,noiseDeadline,idleAt,noiseStart;int attempts=0,noiseContacts=0,beforeContacts=0,afterContacts=0,skippedRegions=0;
  String beforeNoise="",afterNoise="",plan="";TextView status;Targets activeTargets;
  ThumbTapCapture thumbBefore,thumbAfterData;Calibration thumbBase;
  boolean tapAfter=false,tapPreview=false,tapContinue=false,tapLearning=false;long tapDeadline;SingleTapCapture tapBefore;
  String read(String name)throws Exception{return new String(Files.readAllBytes(new File(dir,name).toPath()),StandardCharsets.UTF_8);}
  void record(String name,String body){try{Files.write(new File(dir,name).toPath(),body.getBytes(StandardCharsets.UTF_8));}catch(Exception ignored){}}
  void send(String command,String body){
    if(waiting)return;
    if(command.equals("tap-begin"))tapLearning=true;
    try{
      if(command.equals("noise-learn")||command.equals("noise-retest")){
        retesting=command.equals("noise-retest");noiseCapturing=true;noiseContacts=0;noiseStart=SystemClock.uptimeMillis();
      }
      nonce=UUID.randomUUID().toString();File temp=new File(dir,"request.tmp");
      try(FileOutputStream f=new FileOutputStream(temp)){f.write((command+"\n"+nonce+"\n"+body).getBytes(StandardCharsets.UTF_8));f.getFD().sync();}
      if(!temp.renameTo(new File(dir,"request")))throw new IOException();
      pending=command;waiting=true;deadline=SystemClock.elapsedRealtime()+30000;handler.postDelayed(poll,300);
    }catch(Exception e){noiseCapturing=false;message("连接失败，请重新打开。","关闭",()->finish());}
  }
  final Runnable poll=new Runnable(){public void run(){
    if(!waiting)return;
    try{
      String[] lines=read("reply").split("\n");
      if(lines.length>=2&&lines[0].equals(nonce)){
        waiting=false;String done=pending;pending="";
        if(!lines[1].equals("OK")){noiseCapturing=false;message("操作未完成，请重新打开。","关闭",()->finish());return;}
        if(leaving){
          if(done.equals("noise-learn")||done.equals("noise-retest"))noiseNonce=nonce;
          if(done.equals("noise-apply"))noisePreview=true;
          if(done.equals("preview"))preview=true;
          if(done.equals("tap-apply"))tapPreview=true;
          if(done.equals("tap-save")||done.equals("tap-cancel")){tapPreview=false;tapLearning=false;}
          if(done.equals("noise-save")||done.equals("noise-cancel")){noisePreview=false;noiseNonce="";}
          if(done.equals("save")||done.equals("cancel"))preview=false;
          handler.post(thisActivityCancel);return;
        }
        if(done.equals("tap-begin")||done.equals("tap-retest")){if(tapAfter)tapDeadline=SystemClock.elapsedRealtime()+590000;collecting=true;setContentView(new PointView());return;}
        if(done.equals("tap-finish")){tapLearning=false;record("tap-analysis.txt",read("tap-status"));message("正在调整，请稍候…");send("tap-apply","");return;}
        if(done.equals("tap-apply")){tapPreview=true;tapAfter=true;tapDeadline=SystemClock.elapsedRealtime()+590000;pointIntro();return;}
        if(done.equals("tap-save")){tapPreview=false;flow.confirmTap();coordinateIntro();return;}
        if(done.equals("tap-cancel")){tapLearning=false;tapPreview=false;tapAfter=false;if(tapContinue){tapContinue=false;flow.useOriginalTap();coordinateIntro();}else pointIntro();return;}
        if(done.equals("noise-learn")||done.equals("noise-retest")){
          noiseNonce=nonce;noiseDeadline=SystemClock.elapsedRealtime()+25000;
          message("请保持静置，12秒内不要触摸。");handler.postDelayed(noisePoll,300);return;
        }
        if(done.equals("noise-export")){patch=read("noise-patch");patchReady();return;}
        if(done.equals("noise-apply")){noisePreview=true;flow.changedNoise();prepareRetest();return;}
        if(done.equals("noise-save")){noisePreview=false;noiseNonce="";flow.confirmNoise();pointIntro();return;}
        if(done.equals("noise-cancel")){noisePreview=false;flow.changedNoise();noiseIntro();return;}
        if(done.equals("preview")){preview=true;deadline=SystemClock.elapsedRealtime()+150000;if(thumbAfter)thumbIntro(true);else thumbIntro(false);return;}
        if(done.equals("save")){preview=false;message("已保存。","完成",()->finish());return;}
        if(done.equals("cancel")||done.equals("skip")){preview=false;finish();return;}
      }
    }catch(Exception ignored){}
    if(SystemClock.elapsedRealtime()>deadline){waiting=false;noiseCapturing=false;message("连接超时，请重新打开。","关闭",()->finish());return;}
    handler.postDelayed(this,300);
  }};
  @Override public boolean dispatchTouchEvent(MotionEvent e){
    if(noiseCapturing){int a=e.getActionMasked();if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN)noiseContacts++;return true;}
    return super.dispatchTouchEvent(e);
  }
  @Override public void onCreate(Bundle state){
    super.onCreate(state);dir=createDeviceProtectedStorageContext().getFilesDir();
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|WindowManager.LayoutParams.FLAG_FULLSCREEN);
    getWindow().getDecorView().setSystemUiVisibility(5894);message("正在读取本机触摸服务…");handler.postDelayed(this::load,500);
  }
  void load(){
    try{
      String body=read("context");for(String l:body.split("\n"))if(l.startsWith("Device="))device=l.substring(7);
      if(!device.matches("[0-9a-f]{64}"))throw new IOException();original=new Calibration(body);
      if(isInMultiWindowMode()){message("请全屏打开应用后校准。","关闭",()->finish());return;}
      if(getIntent().getBooleanExtra("point_capture",false)){
        if(getIntent().getBooleanExtra("auto_capture",false))startPointCapture();else pointIntro();
      }else noiseIntro();
    }catch(Exception e){if(++attempts<30)handler.postDelayed(this::load,500);else message("触摸服务未就绪，未开始采集。","关闭",()->finish());}
  }
  LinearLayout layout(String text){
    LinearLayout l=new LinearLayout(this);l.setOrientation(1);l.setGravity(Gravity.CENTER);l.setPadding(50,24,50,24);l.setBackgroundColor(Color.rgb(22,27,36));
    status=new TextView(this);status.setText(text);status.setTextSize(21);status.setTextColor(Color.WHITE);status.setGravity(Gravity.CENTER);l.addView(status);
    ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(l);setContentView(scroll);return l;
  }
  void button(LinearLayout l,String text,Runnable action){Button b=new Button(this);b.setText(text);b.setTextSize(19);b.setOnClickListener(v->{if(!waiting)action.run();});l.addView(b,new LinearLayout.LayoutParams(850,100));}
  void holdButton(LinearLayout l,String text,Runnable action){Button b=new Button(this);b.setText(text);b.setTextSize(19);b.setOnLongClickListener(v->{if(!waiting)action.run();return true;});l.addView(b,new LinearLayout.LayoutParams(850,110));}
  void message(String text){layout(text);}
  void message(String text,String label,Runnable action){LinearLayout l=layout(text);button(l,label,action);}
  void noiseIntro(){
    flow.changedNoise();patchAvailable=false;patch="";noiseBand=false;exportNotice="";
    LinearLayout l=layout("请放稳设备，移开手与触控笔。\n5秒后开始，请保持静置。");
    button(l,"现在检测",()->{cancelCountdown();send("noise-learn","");});
    button(l,"暂不检测，退出",()->{cancelCountdown();finish();});
    idleArmed=true;idleAt=SystemClock.elapsedRealtime()+5000;handler.post(idleCountdown);
  }
  void cancelCountdown(){idleArmed=false;handler.removeCallbacks(idleCountdown);}
  final Runnable idleCountdown=new Runnable(){public void run(){
    if(!idleArmed||isFinishing())return;long left=idleAt-SystemClock.elapsedRealtime();
    if(left<=0){idleArmed=false;send("noise-learn","");return;}
    status.setText("请放稳设备，移开手与笔。\n"+((left+999)/1000)+"秒后开始。");handler.postDelayed(this,200);
  }};
  Map<String,String> details(String[] lines){Map<String,String> m=new HashMap<>();for(int i=1;i<lines.length;i++)for(String w:lines[i].trim().split("\\s+")){int k=w.indexOf('=');if(k>0)m.put(w.substring(0,k),w.substring(k+1));}return m;}
  String noiseSummary(String state,String frames,Map<String,String> info){return "原始信号："+state+" / 帧 "+frames+" / 异常帧 "+info.getOrDefault("abnormal_frames","未知")+"\n本应用全屏窗口收到的新触点："+noiseContacts+" / 观测 "+((SystemClock.uptimeMillis()-noiseStart)/1000.)+" 秒\n电极行："+info.getOrDefault("row_ids","无")+"；列："+info.getOrDefault("column_ids","无")+"；供电："+info.getOrDefault("power_online","未知");}
  final Runnable noisePoll=new Runnable(){public void run(){
    if(SystemClock.elapsedRealtime()>noiseDeadline){noiseCapturing=false;message("检测超时，请重新打开。","关闭",()->finish());return;}
    try{
      String[] lines=read("noise-status").trim().split("\n"),f=lines[0].split("\\s+");
      if(f.length==4&&f[0].equals(noiseNonce)){
        Map<String,String> info=details(lines);
        if(!f[1].equals("learning")){noiseCapturing=false;String summary=noiseSummary(f[1],f[2],info);
          if(retesting){afterContacts=noiseContacts;afterNoise=summary;flow.finishRetest();record("idle-after.txt",summary);noiseReview();}
          else{beforeContacts=noiseContacts;beforeNoise=summary;record("idle-before.txt",summary);noiseBand=info.getOrDefault("candidate_kind","none").equals("band");patchAvailable=f[1].equals("ready")||f[1].equals("line_noise")&&noiseBand;detectionResult();}
          return;
        }
        status.setText("正在检测，请勿触摸。");
      }
    }catch(Exception ignored){}handler.postDelayed(this,300);
  }};
  void detectionResult(){
    LinearLayout l=layout("检测完成，请继续。");
    if(patchAvailable)button(l,"生成修复",()->send("noise-export",""));
    else button(l,"再次检测",this::prepareRetest);
    button(l,"重新检测",this::noiseIntro);button(l,"退出，不改变规则",()->finish());
  }
  void patchReady(){
    LinearLayout l=layout("请保存修复文件，然后开始复测。\n"+exportNotice);
    button(l,"保存补丁文件…",this::exportPatch);
    holdButton(l,"长按：应用并复测",()->send("noise-apply",""));
    button(l,"放弃候选，重新检测",this::noiseIntro);
  }
  void exportPatch(){
    if(patch.isEmpty())return;
    try{exporting=true;Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("application/octet-stream");i.putExtra(Intent.EXTRA_TITLE,"static-touch-"+device.substring(0,12)+".noise");startActivityForResult(i,41);}
    catch(Exception e){exporting=false;exportNotice="暂时无法导出，请重试。";patchReady();}
  }
  @Override protected void onActivityResult(int request,int result,Intent data){
    super.onActivityResult(request,result,data);if(request!=41)return;exporting=false;
    if(result==RESULT_OK&&data!=null&&data.getData()!=null){
      try(OutputStream o=getContentResolver().openOutputStream(data.getData(),"w")){if(o==null)throw new IOException();o.write(patch.getBytes(StandardCharsets.UTF_8));o.flush();exportNotice="文件已保存。";}
      catch(Exception e){exportNotice="保存失败，请重试。";}
    }else exportNotice="已取消文件导出。";patchReady();
  }
  void prepareRetest(){
    LinearLayout l=layout("请放稳设备，移开手与笔。\n准备好后开始。" );
    button(l,"已放稳，开始复测",()->send("noise-retest",noisePreview?"preview":"current"));button(l,"放弃并恢复原规则",()->send("noise-cancel",""));
  }
  void noiseReview(){
    LinearLayout l=layout("复测完成\n请轻点、拖动和双指缩放，确认操作正常。");
    holdButton(l,"长按：操作正常，继续",()->{if(!flow.retested)return;if(noisePreview)send("noise-save","");else{flow.confirmNoise();pointIntro();}});
    button(l,"仍有问题，再次检测",this::prepareRetest);button(l,"效果不接受，恢复原规则",()->send("noise-cancel",""));
  }
  void pointIntro(){
    LinearLayout l=layout((tapAfter?"再测一次":"单指点击检测")+"\n用一根手指轻点黄色十字，然后抬手。共15次。\n采到后等1秒，再点下一个。");
    button(l,"开始",this::startPointCapture);
    button(l,"退出",()->finish());
  }
  void startPointCapture(){if(tapAfter)send("tap-retest","");else{flow.changedTap();send("tap-begin","");}}
  boolean tapAlive(){return !tapAfter||tapPreview&&SystemClock.elapsedRealtime()<tapDeadline;}
  final class PointView extends View {
    final Paint paint=new Paint(3);final String run=UUID.randomUUID().toString();
    final SingleTapCapture test=new SingleTapCapture(SystemClock.uptimeMillis()+3000,0x535058L+device.hashCode());
    final StringBuilder events=new StringBuilder();boolean written=false,truncated=false;int lastCue=-2;
    PointView(){super(MainActivity.this);record("point15-active-run.txt",run+"\n");}
    void logCue(int i,long now){try{JSONObject j=new JSONObject();j.put("kind","cue").put("run",run).put("time_ms",now).put("trial",i);if(i>=0&&i<15)j.put("target",new JSONArray(test.targets[i]));events.append(j).append('\n');}catch(Exception ignored){}}
    void storeResult(boolean complete){
      if(written)return;written=true;test.complete=complete;
      try{JSONObject j=new JSONObject();j.put("run",run).put("device",device).put("start_ms",test.start).put("advance_mode","captured_then_1000ms_and_released").put("after_capture_ms",1000).put("completed_trials",test.completed()).put("shown_at_ms",new JSONArray(test.shownAt)).put("captured_at_ms",new JSONArray(test.capturedAt)).put("ended_at_ms",new JSONArray(test.endedAt)).put("complete",complete).put("truncated",truncated);
        j.put("starts",new JSONArray(test.starts)).put("peak_simultaneous",new JSONArray(test.peak)).put("gesture_groups",new JSONArray(test.groups)).put("cancels",new JSONArray(test.cancels)).put("targets",new JSONArray(test.targets));
        j.put("operator_confirmed",false);record("point15-summary.json",j.toString(2));record("point15-events.jsonl",events.toString());record("point15-context.profile",original.encode(device));record(tapAfter?"tap-after.json":"tap-before.json",j.toString(2));
      }catch(Exception e){message("保存失败，请重新开始。","关闭",()->finish());}
    }
    final Runnable tick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;long now=SystemClock.uptimeMillis();test.tick(now);int i=test.trial(now);
      if(!tapAlive()){collecting=false;storeResult(false);tapContinue=false;send("tap-cancel","");return;}
      if(i!=lastCue){lastCue=i;logCue(i,now);}
      if(test.finished(now)){storeResult(true);collecting=false;if(truncated){message("记录未完整保存，请重新开始。","重试",MainActivity.this::startPointCapture);}else pointResult(test,run);return;}invalidate();handler.postDelayed(this,40);}};
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(tick);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(tick);storeResult(test.finished(SystemClock.uptimeMillis()));super.onDetachedFromWindow();}
    protected void onDraw(Canvas c){float w=getWidth(),h=getHeight();c.drawColor(Color.rgb(22,27,36));long now=SystemClock.uptimeMillis();int i=test.trial(now);
      paint.setColor(Color.WHITE);paint.setTextSize(34);paint.setTextAlign(Paint.Align.CENTER);
      c.drawText("轻点黄色十字，然后抬手",w/2,h*.34f,paint);
      if(i<0){c.drawText("准备 "+((test.start-now+999)/1000)+" 秒",w/2,h*.43f,paint);return;}if(i>=15)return;
      boolean tap=!test.captured();String hint=tap?"请轻点十字":test.remaining(now)>0?String.format(Locale.ROOT,"已采集，%.1f秒后进入下一点",test.remaining(now)/1000.):"已满1秒，请完全抬手后进入下一点";
      c.drawText((i+1)+" / 15  —  "+hint,w/2,h*.43f,paint);
      double[] t=test.targets[i];float x=(float)t[0]*(w-1),y=(float)t[1]*(h-1);paint.setColor(tap?Color.YELLOW:Color.GREEN);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(4);c.drawCircle(x,y,32,paint);c.drawLine(x-44,y,x+44,y,paint);c.drawLine(x,y-44,x,y+44,paint);paint.setStyle(Paint.Style.FILL);
    }
    public boolean onTouchEvent(MotionEvent e){
      if(!collecting)return true;test.event(e.getActionMasked(),e.getPointerCount(),e.getEventTime());
      if(events.length()<2000000){try{JSONObject j=new JSONObject();j.put("kind","event").put("time_ms",e.getEventTime()).put("down_ms",e.getDownTime()).put("trial",test.trial(e.getEventTime())).put("action",e.getActionMasked()).put("action_index",e.getActionIndex());JSONArray pp=new JSONArray();
        for(int i=0;i<e.getPointerCount();i++){JSONObject p=new JSONObject();p.put("id",e.getPointerId(i)).put("x",e.getX(i)/Math.max(1,getWidth()-1)).put("y",e.getY(i)/Math.max(1,getHeight()-1)).put("major_px",e.getTouchMajor(i)).put("minor_px",e.getTouchMinor(i)).put("pressure",e.getPressure(i)).put("tool",e.getToolType(i));pp.put(p);}j.put("pointers",pp);events.append(j).append('\n');
      }catch(Exception ignored){}}else truncated=true;return true;
    }
  }
  void pointResult(SingleTapCapture t,String run){
    if(!tapAfter){
      tapBefore=t;LinearLayout l=layout("点击完成\n刚才是否每个十字都只点了一次？");
      holdButton(l,"长按：是，调整并复测",()->{record("point15-operator-confirmed.txt",run+"\nSINGLE_FINGER_ONE_TAP_PER_TARGET\n");message("正在分析，请稍候…");send("tap-finish","");});
      button(l,"重新采集",this::pointIntro);return;
    }
    flow.tapRetested=true;
    LinearLayout l=layout("复测完成\n请轻点、快速双击、拖动和双指缩放，确认操作正常。");
    addGesturePad(l);
    if(tapBefore!=null&&t.total()<=tapBefore.total()&&t.multiTrials()<=tapBefore.multiTrials())
      holdButton(l,"长按：操作正常，保存并继续",()->{if(tapAlive()){record("tap-confirmed.txt",run+"\nUSER_ACCEPTED\n");send("tap-save","");}});
    button(l,"再测一次",this::startPointCapture);
    button(l,"恢复原设置并继续",()->{tapContinue=true;send("tap-cancel","");});
  }
  void addGesturePad(LinearLayout l){
    View pad=new View(this){final Paint p=new Paint(3);float[] xy=new float[0];
      protected void onDraw(Canvas c){c.drawColor(Color.DKGRAY);p.setColor(Color.YELLOW);for(int i=0;i<xy.length;i+=2)c.drawCircle(xy[i],xy[i+1],15,p);}
      public boolean onTouchEvent(MotionEvent e){getParent().requestDisallowInterceptTouchEvent(true);int a=e.getActionMasked(),skip=a==6?e.getActionIndex():-1,n=a==1||a==3?0:e.getPointerCount()-(skip>=0?1:0);xy=new float[n*2];for(int i=0,j=0;j<n;i++){if(i==skip)continue;xy[j*2]=e.getX(i);xy[j*2+1]=e.getY(i);j++;}invalidate();return true;}};
    l.addView(pad,new LinearLayout.LayoutParams(1000,260));
  }
  void coordinateIntro(){
    if(!flow.noiseConfirmed){prepareRetest();return;}if(!flow.tapConfirmed){pointIntro();return;}
    LinearLayout l=layout("点击黄色十字，变绿后抬手。\n点不到按音量减跳过；音量加返回上一点。" );
    button(l,"开始42次采样",()->{skippedRegions=0;collecting=true;setContentView(new Targets());});
  }
  boolean trialAlive(){if(preview&&SystemClock.elapsedRealtime()<deadline)return true;collecting=false;send("cancel","");message("预览已超时，正在恢复上次保存的坐标结果。");return false;}
  void thumbIntro(boolean after){
    collecting=false;thumbAfter=after;
    LinearLayout l=layout((after?"再测一次":"拇指点击检测")+"\n每个编号出现时，用拇指轻点一次。共10次。" );
    button(l,after?"开始同条件复测":"开始10次点击采集",()->{if(trialAlive()){collecting=true;setContentView(new ThumbView(after));}});
    button(l,"恢复原坐标结果并退出",()->send("cancel",""));
  }
  void thumbPlan(){
    collecting=false;thumbBase=candidate;
    LinearLayout l=layout("点击完成\n刚才是否每个编号都只点了一次？" );
    button(l,"我已按提示点击，生成修复方案",()->{
      if(!trialAlive())return;flow.changedThumb();
      if(thumbBefore.proposeSizeCandidate()&&!candidate.thumbOptimized){candidate=candidate.withThumbOptimization(true);plan="本轮有重复／缺报／短触点证据：候选将接触面积上限从2.0cm放宽到2.4cm；保持触点阈值、起落时序与坐标曲线不变。不是根因结论，需同条件复测。";}
      else if(candidate.thumbOptimized)plan="当前已采用2.4cm上限，不继续盲目放宽。保留现有参数进行同条件复测；若仍有问题，不把它声明为已修复。";
      else plan="本轮未达到生成面积候选的证据门限，保留2.0cm与当前曲线，不无故修改正常触摸；仍进行同条件复测。";
      record("thumb-plan.txt",thumbBefore.encode()+plan);thumbCandidateReview();
    });
    button(l,"刚才操作不一致，重新采集",()->thumbIntro(false));
  }
  void thumbCandidateReview(){LinearLayout l=layout("准备好后，再用拇指点击10次。" );
    button(l,"临时应用方案，再测试10次",()->{thumbAfter=true;send("preview",candidate.encode(device));});
    button(l,"放弃候选，重新采集",()->{candidate=thumbBase;thumbIntro(false);});
  }
  void thumbReview(){
    collecting=false;flow.thumbRetested=true;
    LinearLayout l=layout("复测完成，请确认点击正常。" );
    holdButton(l,"长按：效果确认，进入最后手势确认",()->{if(flow.confirmThumb())review();});
    button(l,"再做同条件复测",()->thumbIntro(true));
    button(l,"修复无效，恢复原拇指参数保留坐标",()->{candidate=thumbBase;thumbAfter=true;plan="已恢复本轮拇指测试前参数；重新复测后由你确认，未标记修复成功。";flow.changedThumb();send("preview",candidate.encode(device));});
    button(l,"放弃全部坐标改动",()->send("cancel",""));
  }
  void review(){
    if(!flow.thumbConfirmed)return;
    LinearLayout l=layout("请在下方轻点、拖动和双指缩放。\n确认操作正常后保存。" );
    View pad=new View(this){final Paint p=new Paint(3);float[] points=new float[0];
      protected void onDraw(Canvas c){c.drawColor(Color.DKGRAY);p.setColor(Color.YELLOW);for(int i=0;i<points.length;i+=2)c.drawCircle(points[i],points[i+1],15,p);}
      public boolean onTouchEvent(MotionEvent e){getParent().requestDisallowInterceptTouchEvent(true);int a=e.getActionMasked(),skip=a==MotionEvent.ACTION_POINTER_UP?e.getActionIndex():-1,n=a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL?0:e.getPointerCount()-(skip>=0?1:0);points=new float[n*2];for(int i=0,j=0;j<n;i++){if(i==skip)continue;points[j*2]=e.getX(i);points[j*2+1]=e.getY(i);j++;}invalidate();return true;}};
    l.addView(pad,new LinearLayout.LayoutParams(1000,230));
    holdButton(l,"长按：位置及手势可接受，保存本机结果",()->{if(trialAlive())send("save","");});
    button(l,"恢复上次坐标结果",()->send("cancel",""));
  }
  final class ThumbView extends View {
    final boolean after;final Paint p=new Paint(3);final ThumbTapCapture capture;float x=-1,y;
    ThumbView(boolean a){super(MainActivity.this);after=a;capture=new ThumbTapCapture(SystemClock.uptimeMillis()+3000);}
    final Runnable tick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;if(!trialAlive())return;
      if(capture.finished(SystemClock.uptimeMillis())){collecting=false;if(after){thumbAfterData=capture;record("thumb-after.txt",capture.encode());thumbReview();}else{thumbBefore=capture;record("thumb-before.txt",capture.encode());thumbPlan();}return;}invalidate();handler.postDelayed(this,80);}};
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(tick);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(tick);super.onDetachedFromWindow();}
    protected void onDraw(Canvas c){float w=getWidth(),h=getHeight();c.drawColor(Color.rgb(22,27,36));p.setColor(Color.WHITE);p.setTextAlign(Paint.Align.CENTER);p.setTextSize(36);long now=SystemClock.uptimeMillis();int r=capture.round(now);
      c.drawText((after?"修复后":"修复前")+"拇指点击采集",w/2,h*.30f,p);
      c.drawText(now<capture.start?"准备：请在每个编号出现时点击一次":"现在点击一次："+(r+1)+" / 10",w/2,h*.39f,p);
      c.drawText("在容易出问题的位置点击，不必追逐上报位置",w/2,h*.47f,p);
      c.drawText("已记录："+capture.total()+"",w/2,h*.55f,p);
      if(x>=0){p.setColor(Color.YELLOW);c.drawCircle(x,y,16,p);}}
    public boolean onTouchEvent(MotionEvent e){if(!collecting)return true;int a=e.getActionMasked(),i=e.getActionIndex();x=e.getX(i);y=e.getY(i);
      if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN)capture.down(e.getPointerId(i),e.getEventTime());
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_POINTER_UP)capture.up(e.getPointerId(i),e.getEventTime());
      invalidate();return true;}
  }
  final class Targets extends View {
    final Paint p=new Paint(3);final double[][] samples=new double[42][];final CalibrationCapture capture=new CalibrationCapture();
    int index=0;boolean pendingTap=false;String hint="";
    Targets(){super(MainActivity.this);activeTargets=this;}
    double[] target(){int g=index/14,k=(index%14)/2;return g==0?new double[]{Calibration.POS[k],.5}:g==1?new double[]{.5,Calibration.POS[k]}:new double[]{Calibration.POS[k],.04};}
    protected void onDraw(Canvas c){float w=getWidth(),h=getHeight();c.drawColor(Color.rgb(22,27,36));double[] t=target();float x=(float)t[0]*(w-1),y=(float)t[1]*(h-1);
      p.setColor(Color.WHITE);p.setTextAlign(Paint.Align.CENTER);p.setTextSize(34);c.drawText("全屏偏移采集 "+(index+1)+" / 42",w/2,h*.34f,p);
      c.drawText("点击黄色十字，变绿后抬手",w/2,h*.40f,p);
      c.drawText("音量减：跳过此点；音量加：重采上一个点",w/2,h*.46f,p);c.drawText(hint,w/2,h*.55f,p);
      p.setColor(pendingTap||capture.green(SystemClock.uptimeMillis())?Color.GREEN:Color.YELLOW);p.setStrokeWidth(3);p.setStyle(Paint.Style.STROKE);c.drawCircle(x,y,23,p);c.drawLine(x-32,y,x+32,y,p);c.drawLine(x,y-32,x,y+32,p);p.setStyle(Paint.Style.FILL);}
    void advance(){capture.cancel();if(index>=42){collecting=false;candidate=Calibration.fitPartial(samples,original).withThumbOptimization(original.thumbOptimized);record("coordinate-candidate.profile",candidate.encode(device));thumbAfter=false;message("正在调整，请稍候…");send("preview",candidate.encode(device));}else{pendingTap=false;invalidate();}}
    void skipRegion(){if(!collecting||pendingTap)return;int k=index/2*2;samples[k]=samples[k+1]=null;index=k+2;skippedRegions++;hint="已跳过";advance();}
    void previous(){if(!collecting||pendingTap||index==0)return;index--;samples[index]=null;capture.cancel();hint="重新采集此点";invalidate();}
    public boolean onTouchEvent(MotionEvent e){if(!collecting||pendingTap)return true;int a=e.getActionMasked(),i=e.getActionIndex();double w=Math.max(1,getWidth()-1),h=Math.max(1,getHeight()-1);
      if(a==MotionEvent.ACTION_CANCEL){capture.cancel();return true;}
      if(a==MotionEvent.ACTION_DOWN)capture.cancel();
      if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN){double[] t=target();if(capture.begin(e.getPointerId(i),e.getEventTime(),e.getX(i)/w,e.getY(i)/h,t[0],t[1]))handler.postDelayed(()->{if(isAttachedToWindow())invalidate();},90);}
      if(a==MotionEvent.ACTION_MOVE){int k=e.findPointerIndex(capture.pointer);if(k>=0){for(int j=0;j<e.getHistorySize();j++)capture.move(capture.pointer,e.getHistoricalX(k,j)/w,e.getHistoricalY(k,j)/h);capture.move(capture.pointer,e.getX(k)/w,e.getY(k)/h);}}
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_POINTER_UP){double[] s=capture.end(e.getPointerId(i),e.getEventTime(),e.getX(i)/w,e.getY(i)/h);if(s!=null){samples[index]=original.inverse(s[0],s[1]);hint="已采集，请抬手";pendingTap=true;handler.postDelayed(()->{if(collecting&&isAttachedToWindow()){index++;advance();}},500);}}
      invalidate();return true;}
    @Override protected void onDetachedFromWindow(){capture.cancel();super.onDetachedFromWindow();}
  }
  @Override public boolean onKeyDown(int key,KeyEvent e){if(collecting&&activeTargets!=null&&activeTargets.isAttachedToWindow()){if(key==KeyEvent.KEYCODE_VOLUME_DOWN){if(e.getRepeatCount()==0)activeTargets.skipRegion();return true;}if(key==KeyEvent.KEYCODE_VOLUME_UP){if(e.getRepeatCount()==0)activeTargets.previous();return true;}}return super.onKeyDown(key,e);}
  @Override public void onBackPressed(){finish();}
  final Runnable thisActivityCancel=()->cancelOnLeave();
  @Override public void onPause(){super.onPause();if(exporting)return;leaving=true;noiseCapturing=false;cancelCountdown();collecting=false;handler.removeCallbacks(noisePoll);
    // Let a pending bridge request finish before requesting rollback; never replace its nonce.
    handler.postDelayed(this::cancelOnLeave,100);if(!isFinishing())finish();
  }
  void cancelOnLeave(){if(waiting){handler.postDelayed(this::cancelOnLeave,300);return;}if(tapPreview||tapLearning){tapPreview=false;tapLearning=false;send("tap-cancel","");}else if(noisePreview||!noiseNonce.isEmpty()){noisePreview=false;noiseNonce="";send("noise-cancel","");}else if(preview){preview=false;send("cancel","");}}
}
