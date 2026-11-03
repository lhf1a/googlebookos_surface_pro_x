package org.surface.touchcalibration;
import android.app.Activity;
import android.os.*;
import android.graphics.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

public class MainActivity extends Activity {
  File dir; String device="",nonce=""; Calibration original,candidate;
  Handler handler=new Handler(); boolean preview=false,waiting=false,collecting=false;
  long deadline; String pending=""; Targets activeTargets; int skippedRegions=0,skippedChecks=0; boolean noisePreview=false; String noiseNonce=""; long noiseDeadline; boolean idleArmed=false; long idleAt; TextView status; boolean previewForThumb=false;
  String read(String name)throws Exception{return new String(Files.readAllBytes(new File(dir,name).toPath()),StandardCharsets.UTF_8);}
  void send(String command,String body){
    try{
      nonce=UUID.randomUUID().toString();File temp=new File(dir,"request.tmp");
      try(FileOutputStream f=new FileOutputStream(temp)){f.write((command+"\n"+nonce+"\n"+body).getBytes(StandardCharsets.UTF_8));f.getFD().sync();}
      if(!temp.renameTo(new File(dir,"request")))throw new IOException();
      pending=command;waiting=true;deadline=SystemClock.elapsedRealtime()+30000;handler.postDelayed(poll,300);
    }catch(Exception e){message("请求未发送，请稍后重新打开校准。");}
  }
  final Runnable poll=new Runnable(){public void run(){
    if(!waiting)return;
    try{
      String reply=read("reply");String[] lines=reply.split("\n");
      if(lines.length>=2&&lines[0].equals(nonce)){
        waiting=false;
        if(!lines[1].equals("OK")){preview=false;message("未应用校准，已保留上一份结果。请重新打开后再试。");return;}
        if(pending.equals("noise-learn")){noiseNonce=nonce;noiseDeadline=SystemClock.elapsedRealtime()+25000;message("静置检测中（约12秒）\n请勿触摸屏幕。会报告条带、大片和短暂异常；\n发现异常不等于可以安全学习屏蔽规则，任何规则均不会自动保存。");handler.postDelayed(noisePoll,500);return;}
        if(pending.equals("noise-apply")){noisePreview=true;noiseReview();return;}
        if(pending.equals("noise-save")||pending.equals("noise-cancel")){noisePreview=false;coordinateIntro();return;}
        if(pending.equals("preview")){preview=true;deadline=SystemClock.elapsedRealtime()+150000;if(previewForThumb)thumbIntro();else startValidation();return;}
        if(pending.equals("save")){preview=false;message("校准已保存。以后开机会自动使用本机结果。", "完成",()->finish());return;}
        if(pending.equals("cancel")||pending.equals("skip")){preview=false;finish();return;}
      }
    }catch(Exception ignored){}
    if(SystemClock.elapsedRealtime()>deadline){waiting=false;preview=false;message("服务暂未响应。临时校准会自动恢复，请稍后再试。");return;}
    handler.postDelayed(this,300);
  }};
  @Override public void onCreate(Bundle state){
    super.onCreate(state);dir=createDeviceProtectedStorageContext().getFilesDir();
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|WindowManager.LayoutParams.FLAG_FULLSCREEN);
    getWindow().getDecorView().setSystemUiVisibility(5894);
    // Creating the files directory also allows the boot helper to publish context.
    message("正在读取本机校准状态…");handler.postDelayed(this::load,500);
  }
  int attempts=0;
  void load(){
    try{String body=read("context");for(String line:body.split("\n"))if(line.startsWith("Device="))device=line.substring(7);
      if(!device.matches("[0-9a-f]{64}"))throw new IOException();original=new Calibration(body);
      noiseIntro();
    }catch(Exception e){if(++attempts<30)handler.postDelayed(this::load,500);else message("校准服务尚未就绪。请稍后重新打开。", "关闭",()->finish());}
  }
  LinearLayout layout(String text){
    LinearLayout l=new LinearLayout(this);l.setOrientation(1);l.setGravity(Gravity.CENTER);l.setPadding(80,40,80,40);l.setBackgroundColor(Color.rgb(22,27,36));
    status=new TextView(this);status.setText(text);status.setTextSize(23);status.setTextColor(Color.WHITE);status.setGravity(Gravity.CENTER);l.addView(status);setContentView(l);return l;
  }
  void button(LinearLayout l,String text,Runnable action){Button b=new Button(this);b.setText(text);b.setTextSize(20);b.setOnClickListener(v->action.run());l.addView(b,new LinearLayout.LayoutParams(600,100));}
  void message(String text){layout(text);}
  void message(String text,String label,Runnable action){LinearLayout l=layout(text);button(l,label,action);}
  void menu(){
    if(isInMultiWindowMode()){message("请先将校准窗口全屏，再重新打开。窗口模式无法准确测量整块屏幕。","关闭",()->finish());return;}
    LinearLayout l=layout("触屏校准\n\n先检测本机静置噪声，再进行坐标补偿。请用一根手指自然点击黄色十字，\n每个位置两次，最多42次；之后检查5个位置。点不到可跳过，音量减键也可跳过。\n过程中不必追着偏移的触点移动。原结果会保留到你确认保存。\n\n可跳过，之后从应用列表打开“触屏校准”。");
    button(l,"先静置检测，再校准",this::noiseIntro);
    button(l,"保留现有缓解，直接坐标校准",this::coordinateIntro);
    button(l,"仅检查拇指",()->{candidate=original;previewForThumb=true;message("正在准备拇指测试…");send("preview",candidate.encode(device));});
    button(l,"暂时跳过",()->send("skip",""));
  }
  final double[][] validation={{.5,.5},{.03,.5},{.97,.5},{.5,.03},{.5,.97}};
  void startValidation(){collecting=true;setContentView(new Targets(true));}
  void review(boolean pass){
    collecting=false;
    if(!pass){LinearLayout l=layout("检查点仍有明显偏差，本次结果不保存。\n请返回后重新校准；原结果会恢复。");button(l,"恢复原结果",()->send("cancel",""));return;}
    LinearLayout l=layout("请在下方区域轻点 3 次、拖动，再双指张合 2 次。\n确认位置、跟手和双指均正常后保存。\n跳过区域："+skippedRegions+"，未检查点："+skippedChecks+"（不等于全屏已通过）。\n拇指优化："+(candidate.thumbOptimized?"已启用":"未启用")+"；未保存的试用会自动恢复。");
    View pad=new View(this){Paint p=new Paint(3);float[] points=new float[0];protected void onDraw(Canvas c){c.drawColor(Color.DKGRAY);p.setColor(Color.YELLOW);for(int i=0;i<points.length;i+=2)c.drawCircle(points[i],points[i+1],15,p);}public boolean onTouchEvent(MotionEvent e){int a=e.getActionMasked();int skip=a==MotionEvent.ACTION_POINTER_UP?e.getActionIndex():-1;int n=a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL?0:e.getPointerCount()-(skip>=0?1:0);points=new float[n*2];for(int i=0,j=0;j<n;i++){if(i==skip)continue;points[j*2]=e.getX(i);points[j*2+1]=e.getY(i);j++;}invalidate();return true;}};
    l.addView(pad,new LinearLayout.LayoutParams(1000,260));button(l,"轻点、拖动和双指正常，保存",()->{if(trialAlive())send("save","");});button(l,"恢复原结果",()->send("cancel",""));
  }
  boolean trialAlive(){if(preview&&SystemClock.elapsedRealtime()<deadline)return true;collecting=false;send("cancel","");message("试用时间已结束，正在恢复原结果。");return false;}
  void thumbIntro(){
    collecting=false;
    LinearLayout l=layout("边缘拇指测试\n\n用大拇指在容易出问题的边缘慢拖，保持接触 5 秒，重复 3 次。\n倒计时结束后才抬手。若提前断触或重复点击，可选择优化并重测。\n\n当前跳过区域："+skippedRegions+"，未检查点："+skippedChecks+"（不等于全屏已通过）。\n拇指优化："+(candidate.thumbOptimized?"已启用":"未启用"));
    button(l,"开始边缘慢拖测试",()->{if(trialAlive()){collecting=true;setContentView(new ThumbView());}});
    if(candidate.thumbOptimized==original.thumbOptimized)button(l,"本次跳过拇指测试",()->review(true));
    button(l,"恢复原结果",()->send("cancel",""));
  }
  void thumbIssue(){
    collecting=false;
    LinearLayout l=layout("检测到提前抬起，或你报告了断触／重复点击。\n如果刚才主动抬手，请重新测试。\n只有拇指仍贴着屏幕却断触时，才建议尝试优化。\n"+(candidate.thumbOptimized?"当前已启用优化；若问题仍在，请保留原结果并反馈。":"优化会放宽大拇指接触面积限制，启用后需要重新测试。"));
    button(l,"重新测试",()->{if(trialAlive()){collecting=true;setContentView(new ThumbView());}});
    if(!candidate.thumbOptimized)button(l,"启用拇指优化并重测",()->{candidate=candidate.withThumbOptimization(true);previewForThumb=true;message("正在临时启用优化…");send("preview",candidate.encode(device));});
    button(l,"恢复原结果",()->send("cancel",""));
  }
  final class ThumbView extends View {
    final ThumbCheck test=new ThumbCheck();final Paint p=new Paint(3);String hint="沿边缘慢拖，倒计时结束后再抬手";float x=-1,y;
    final Runnable tick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;if(!trialAlive())return;invalidate();handler.postDelayed(this,100);}};
    ThumbView(){super(MainActivity.this);}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(tick);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(tick);super.onDetachedFromWindow();}
    protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight();c.drawColor(Color.rgb(22,27,36));p.setColor(Color.rgb(40,70,85));c.drawRect(0,0,w,h*.08f,p);c.drawRect(0,h*.92f,w,h,p);c.drawRect(0,0,w*.08f,h,p);c.drawRect(w*.92f,0,w,h,p);
      p.setColor(Color.WHITE);p.setTextAlign(Paint.Align.CENTER);p.setTextSize(36);c.drawText("拇指边缘慢拖 "+(test.completed+1)+" / 3",w/2,h*.35f,p);
      String time=!test.down?"请从蓝色边缘区域落指":test.remaining(SystemClock.uptimeMillis())==0?"可以抬手了":String.format(Locale.ROOT,"保持接触 %.1f 秒",test.remaining(SystemClock.uptimeMillis())/1000.);
      c.drawText(time,w/2,h*.42f,p);c.drawText(hint,w/2,h*.49f,p);
      p.setColor(Color.rgb(70,80,95));c.drawRect(w*.25f,h*.58f,w*.75f,h*.70f,p);p.setColor(Color.WHITE);c.drawText("无法连续拖动／出现重复点击",w/2,h*.65f,p);
      if(test.down){p.setColor(Color.YELLOW);c.drawCircle(x,y,18,p);}
    }
    public boolean onTouchEvent(MotionEvent e){
      if(!collecting||!trialAlive())return true;int a=e.getActionMasked();x=e.getX();y=e.getY();double nx=x/(getWidth()-1.),ny=y/(getHeight()-1.);
      if(e.getPointerCount()>1){test.cancel();hint="本步骤只用一根拇指，请抬手后重试";invalidate();return true;}
      if(a==MotionEvent.ACTION_DOWN){if(nx>.25&&nx<.75&&ny>.58&&ny<.70){thumbIssue();return true;}if(!test.begin(e.getEventTime(),nx,ny))hint="请在蓝色边缘区域落指";else hint="沿边缘移动一小段，保持接触直到提示抬手";}
      else if(a==MotionEvent.ACTION_MOVE){for(int i=0;i<e.getHistorySize();i++)test.move(e.getHistoricalX(i)/(getWidth()-1.),e.getHistoricalY(i)/(getHeight()-1.));test.move(nx,ny);}
      else if(a==MotionEvent.ACTION_UP){String result=test.end(e.getEventTime(),nx,ny);if(result.equals("early")){thumbIssue();return true;}if(result.equals("complete")){review(true);return true;}hint=result.equals("retry")?"请保持在边缘，并沿边缘拖动一小段后重试":"本次完成，请抬手后开始下一次";}
      else if(a==MotionEvent.ACTION_CANCEL){test.cancel();hint="测试被中断，请重新落指";}
      invalidate();return true;
    }
  }
  void coordinateIntro(){
    LinearLayout l=layout("坐标校准\n每个区域两次；点不到可按中央跳过按钮或音量减键。\n跳过区域沿用原曲线锚点，不伪造采样或禁用该区域。\n校准失败或退出会保留原设置。");
    button(l,"开始坐标校准",()->{skippedRegions=skippedChecks=0;collecting=true;setContentView(new Targets(false));});
  }
  void noiseIntro(){
    LinearLayout l=layout("静置检测幽灵触控\n请先擦干屏幕、放稳设备，移开手和触控笔。\n接下来约12秒不要触摸；检测条带、大片干扰及孤立尖峰。\n宽区域异常会明确报告，但不会自动屏蔽该区域。\n候选需预览确认，不能保证修复所有硬件故障。");
    button(l,"现在开始",()->{cancelIdleCountdown();send("noise-learn","");});
    button(l,"取消检测，保留原缓解",()->{cancelIdleCountdown();coordinateIntro();});
    button(l,"其他校准选项",()->{cancelIdleCountdown();menu();});
    idleArmed=true;idleAt=SystemClock.elapsedRealtime()+5000;handler.post(idleCountdown);
  }
  void cancelIdleCountdown(){idleArmed=false;handler.removeCallbacks(idleCountdown);}
  final Runnable idleCountdown=new Runnable(){public void run(){
    if(!idleArmed||isFinishing())return;
    long left=idleAt-SystemClock.elapsedRealtime();
    if(left<=0){idleArmed=false;send("noise-learn","");return;}
    status.setText("静置检测将在 "+((left+999)/1000)+" 秒后自动开始\n\n请放稳设备，移开手和触控笔，保持屏幕干燥。\n检测约12秒；区分“发现异常”和“可安全缓解”。\n不会自动应用、保存或屏蔽整片触摸区域。");
    handler.postDelayed(this,200);
  }};
  Map<String,String> noiseDetails(String[] lines){
    Map<String,String> m=new HashMap<>();
    for(int i=1;i<lines.length;i++)for(String word:lines[i].trim().split("\\s+")){
      int p=word.indexOf('=');if(p>0)m.put(word.substring(0,p),word.substring(p+1));
    }
    return m;
  }
  void detectionResult(String state,String frames,String cells,Map<String,String> info){
    String summary="\n收到原始帧："+frames+"；异常帧："+info.getOrDefault("abnormal_frames","未知")+
      "\n采样开始时充电："+(info.getOrDefault("power_online","unknown").equals("1")?"是":info.getOrDefault("power_online","unknown").equals("0")?"否":"未知");
    LinearLayout l;
    if(state.equals("ready")){
      l=layout("检测到可疑孤立尖峰："+cells+" 个电极。"+summary+"\n可临时预览局部插值；正常相邻接触仍保留，不屏蔽整片区域。");
      button(l,"临时预览缓解",()->send("noise-apply",""));
    }else{
      String title;
      if(state.equals("line_noise"))title="检测到持续条带异常！\n电极行（从0起）："+info.getOrDefault("row_ids","未知")+"；列："+info.getOrDefault("column_ids","未知")+"\n若期间未触摸，这就是静置干扰信号，不是检测正常。\n不适合当作少量坏点自动学习；请检查充电连接条件后重测。";
      else if(state.equals("broad_signal"))title="检测到大片／连续异常信号。\n若已保持静置，可能存在幽灵触控或供电干扰；\n也需排除真实触碰。未自动屏蔽或保存任何区域。";
      else if(state.equals("transient"))title="检测到短暂异常／接触信号。\n本次不是“未发现问题”，但证据不足以生成安全缓解规则。\n请保持静置并对照充电条件重测。";
      else if(state.equals("quiet"))title="本次收到的原始帧未见明显异常。\n不代表所有充电条件下正常，也不是硬件无故障证明。";
      else if(state.equals("no_data"))title="静置检测数据不足／没有新报告，无法判断。\n设备静置时可能停止发送报告；这里没有判定为正常。";
      else title="检测没有完成：状态 "+state+"。\n保留原缓解，不将检测失败显示为没有幽灵触控。";
      l=layout(title+summary);
    }
    button(l,"重新静置检测",this::noiseIntro);
    button(l,"保留原缓解，继续坐标校准",()->send("noise-cancel",""));
  }
  final Runnable noisePoll=new Runnable(){public void run(){
    if(SystemClock.elapsedRealtime()>noiseDeadline){message("检测状态通道超时，无法判断，不代表正常。\n未应用任何新规则。","继续坐标校准",()->send("noise-cancel",""));return;}
    try{
      String[] lines=read("noise-status").trim().split("\n");
      String[] f=lines[0].trim().split("\\s+");
      if(f.length==4&&f[0].equals(noiseNonce)){
        Map<String,String> info=noiseDetails(lines);
        if(!f[1].equals("learning")){detectionResult(f[1],f[2],f[3],info);return;}
        status.setText("静置检测中，请勿触摸（约12秒）\n原始帧："+f[2]+"；异常帧："+info.getOrDefault("abnormal_frames","0")+"\n完成后会分别报告干扰类型和能否安全缓解，不自动保存规则。");
      }
    }catch(Exception ignored){}handler.postDelayed(this,500);
  }};
  void noiseReview(){
    LinearLayout l=layout("缓解正在临时预览（最长180秒）。\n请轻点、拖动并双指张合；确认没有丢失正常触摸后再保存。\n有异常请恢复原规则，未知类型的幽灵触控不会盲目屏蔽。");
    button(l,"正常，保存本机缓解并继续校准",()->send("noise-save",""));
    button(l,"恢复原缓解并继续校准",()->send("noise-cancel",""));
  }
  @Override public boolean onKeyDown(int key,KeyEvent event){
    if(key==KeyEvent.KEYCODE_VOLUME_DOWN&&collecting&&activeTargets!=null&&activeTargets.isAttachedToWindow()){
      activeTargets.skipRegion();return true;
    }return super.onKeyDown(key,event);
  }
  @Override public void onBackPressed(){if(noisePreview)send("noise-cancel","");else if(preview)send("cancel","");else finish();}
  @Override public void onPause(){super.onPause();cancelIdleCountdown();collecting=false;handler.removeCallbacks(noisePoll);if(noisePreview||pending.equals("noise-learn")||pending.equals("noise-apply")){noisePreview=false;send("noise-cancel","");}if(preview||pending.equals("preview")){preview=false;send("cancel","");}}
  final class Targets extends View {
    final boolean check;final Paint p=new Paint(3);final double[][] samples=new double[42][];
    int index=0,count;boolean pendingTap=false,down=false,invalid=false;double sx,sy,minX,maxX,minY,maxY;long downTime;String hint="";boolean passed=true;
    Targets(boolean c){super(MainActivity.this);check=c;activeTargets=this;}
    double[] target(){if(check)return validation[index];int group=index/14,point=(index%14)/2;return group==0?new double[]{Calibration.POS[point],.5}:group==1?new double[]{.5,Calibration.POS[point]}:new double[]{Calibration.POS[point],.04};}
    protected void onDraw(Canvas c){
      c.drawColor(Color.rgb(22,27,36));p.setColor(Color.WHITE);p.setTextSize(36);p.setTextAlign(Paint.Align.CENTER);
      c.drawText((check?"检查位置 ":"校准 ")+(index+1)+" / "+(check?5:42),getWidth()/2f,getHeight()*.36f,p);
      c.drawText("只点黄色十字中心，变绿后抬手，等它恢复黄色再点",getWidth()/2f,getHeight()*.40f,p);
      c.drawText(hint,getWidth()/2f,getHeight()*.44f,p);
      p.setColor(Color.rgb(65,80,100));c.drawRect(getWidth()*.30f,getHeight()*.65f,getWidth()*.70f,getHeight()*.77f,p);
      p.setColor(Color.WHITE);c.drawText("此区域点不到：跳过 / 音量减",getWidth()/2f,getHeight()*.72f,p);
      double[] t=target();float x=(float)t[0]*(getWidth()-1),y=(float)t[1]*(getHeight()-1);
      p.setColor(pendingTap?Color.GREEN:Color.YELLOW);p.setStrokeWidth(3);p.setStyle(Paint.Style.STROKE);c.drawCircle(x,y,23,p);c.drawLine(x-32,y,x+32,y,p);c.drawLine(x,y-32,x,y+32,p);p.setStyle(Paint.Style.FILL);
    }
    void advance(){
          if(index>=(check?5:42)){
            collecting=false;if(check){if(passed)thumbIntro();else review(false);return;}
            try{candidate=Calibration.fitPartial(samples,original).withThumbOptimization(original.thumbOptimized);previewForThumb=false;message("正在应用临时结果，随后检查 5 个位置…");send("preview",candidate.encode(device));}
            catch(IllegalArgumentException ex){message(ex.getMessage()+"\n上一份结果未改变。","重新开始",()->{collecting=true;setContentView(new Targets(false));});}
          }else{pendingTap=false;invalidate();}
    }
    void skipRegion(){
      if(!collecting||pendingTap)return;
      down=false;invalid=true;
      if(check){skippedChecks++;index++;}else{
        int start=(index/2)*2;samples[start]=samples[start+1]=null;index=start+2;skippedRegions++;
      }
      hint="该区域未测量：保留原曲线锚点，不屏蔽触摸";advance();
    }
    void sample(float px,float py){double x=px/(getWidth()-1.),y=py/(getHeight()-1.);if(!check){double[] raw=original.inverse(x,y);x=raw[0];y=raw[1];}sx+=x;sy+=y;count++;minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}
    public boolean onTouchEvent(android.view.MotionEvent e){
      if(!collecting||pendingTap)return true;
      if(e.getActionMasked()==MotionEvent.ACTION_DOWN && e.getX()>getWidth()*.30f && e.getX()<getWidth()*.70f && e.getY()>getHeight()*.65f && e.getY()<getHeight()*.77f){skipRegion();return true;}
      if(check&&SystemClock.elapsedRealtime()>deadline){collecting=false;send("cancel","");message("试用时间已结束，正在恢复原结果。");return true;}
      int action=e.getActionMasked();if(e.getPointerCount()!=1)invalid=true;
      if(action==MotionEvent.ACTION_DOWN){down=true;invalid=false;sx=sy=0;count=0;minX=minY=10;maxX=maxY=-10;downTime=e.getEventTime();sample(e.getX(),e.getY());}
      else if(action==MotionEvent.ACTION_MOVE&&down){for(int i=0;i<e.getHistorySize();i++)sample(e.getHistoricalX(i),e.getHistoricalY(i));sample(e.getX(),e.getY());}
      else if(action==MotionEvent.ACTION_UP&&down){
        sample(e.getX(),e.getY());down=false;long duration=e.getEventTime()-downTime;double x=sx/count,y=sy/count;double[] t=target();
        if(invalid||duration<25||duration>1500||maxX-minX>.025||maxY-minY>.025||Math.abs(x-t[0])>.1||Math.abs(y-t[1])>.1){hint="这次有滑动或落点太远，请在同一位置重试";invalidate();return true;}
        if(check)passed&=Math.abs(x-t[0])<=.025&&Math.abs(y-t[1])<=.025;else samples[index]=new double[]{x,y};
        hint="";pendingTap=true;invalidate();handler.postDelayed(()->{
          if(!collecting)return;
          index++;advance();
        },600);
      }else if(action==MotionEvent.ACTION_CANCEL){down=false;}
      return true;
    }
  }
}
