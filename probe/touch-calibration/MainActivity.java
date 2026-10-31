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
  long deadline; String pending=""; TextView status; boolean previewForThumb=false;
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
      menu();
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
    LinearLayout l=layout("触屏校准\n\n不同屏幕需要不同补偿。请用一根手指自然点击黄色十字，\n每个位置两次，共 42 次；之后检查 5 个位置。\n过程中不必追着偏移的触点移动。原结果会保留到你确认保存。\n\n可跳过，之后从应用列表打开“触屏校准”。");
    button(l,"开始校准",()->{collecting=true;setContentView(new Targets(false));});
    button(l,"仅检查拇指",()->{candidate=original;previewForThumb=true;message("正在准备拇指测试…");send("preview",candidate.encode(device));});
    button(l,"暂时跳过",()->send("skip",""));
  }
  final double[][] validation={{.5,.5},{.03,.5},{.97,.5},{.5,.03},{.5,.97}};
  void startValidation(){collecting=true;setContentView(new Targets(true));}
  void review(boolean pass){
    collecting=false;
    if(!pass){LinearLayout l=layout("检查点仍有明显偏差，本次结果不保存。\n请返回后重新校准；原结果会恢复。");button(l,"恢复原结果",()->send("cancel",""));return;}
    LinearLayout l=layout("请在下方区域轻点 3 次、拖动，再双指张合 2 次。\n确认位置、跟手和双指均正常后保存。\n拇指优化："+(candidate.thumbOptimized?"已启用":"未启用")+"；未保存的试用会自动恢复。");
    View pad=new View(this){Paint p=new Paint(3);float[] points=new float[0];protected void onDraw(Canvas c){c.drawColor(Color.DKGRAY);p.setColor(Color.YELLOW);for(int i=0;i<points.length;i+=2)c.drawCircle(points[i],points[i+1],15,p);}public boolean onTouchEvent(MotionEvent e){int a=e.getActionMasked();int skip=a==MotionEvent.ACTION_POINTER_UP?e.getActionIndex():-1;int n=a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL?0:e.getPointerCount()-(skip>=0?1:0);points=new float[n*2];for(int i=0,j=0;j<n;i++){if(i==skip)continue;points[j*2]=e.getX(i);points[j*2+1]=e.getY(i);j++;}invalidate();return true;}};
    l.addView(pad,new LinearLayout.LayoutParams(1000,260));button(l,"轻点、拖动和双指正常，保存",()->{if(trialAlive())send("save","");});button(l,"恢复原结果",()->send("cancel",""));
  }
  boolean trialAlive(){if(preview&&SystemClock.elapsedRealtime()<deadline)return true;collecting=false;send("cancel","");message("试用时间已结束，正在恢复原结果。");return false;}
  void thumbIntro(){
    collecting=false;
    LinearLayout l=layout("边缘拇指测试\n\n用大拇指在容易出问题的边缘慢拖，保持接触 5 秒，重复 3 次。\n倒计时结束后才抬手。若提前断触或重复点击，可选择优化并重测。\n\n当前拇指优化："+(candidate.thumbOptimized?"已启用":"未启用"));
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
  @Override public void onBackPressed(){if(preview)send("cancel","");else finish();}
  @Override public void onPause(){super.onPause();collecting=false;if(preview||pending.equals("preview")){preview=false;send("cancel","");}}
  final class Targets extends View {
    final boolean check;final Paint p=new Paint(3);final double[][] samples=new double[42][2];
    int index=0,count;boolean pendingTap=false,down=false,invalid=false;double sx,sy,minX,maxX,minY,maxY;long downTime;String hint="";boolean passed=true;
    Targets(boolean c){super(MainActivity.this);check=c;}
    double[] target(){if(check)return validation[index];int group=index/14,point=(index%14)/2;return group==0?new double[]{Calibration.POS[point],.5}:group==1?new double[]{.5,Calibration.POS[point]}:new double[]{Calibration.POS[point],.04};}
    protected void onDraw(Canvas c){
      c.drawColor(Color.rgb(22,27,36));p.setColor(Color.WHITE);p.setTextSize(36);p.setTextAlign(Paint.Align.CENTER);
      c.drawText((check?"检查位置 ":"校准 ")+(index+1)+" / "+(check?5:42),getWidth()/2f,getHeight()*.36f,p);
      c.drawText("只点黄色十字中心，变绿后抬手，等它恢复黄色再点",getWidth()/2f,getHeight()*.40f,p);
      c.drawText(hint,getWidth()/2f,getHeight()*.44f,p);
      double[] t=target();float x=(float)t[0]*(getWidth()-1),y=(float)t[1]*(getHeight()-1);
      p.setColor(pendingTap?Color.GREEN:Color.YELLOW);p.setStrokeWidth(3);p.setStyle(Paint.Style.STROKE);c.drawCircle(x,y,23,p);c.drawLine(x-32,y,x+32,y,p);c.drawLine(x,y-32,x,y+32,p);p.setStyle(Paint.Style.FILL);
    }
    void sample(float px,float py){double x=px/(getWidth()-1.),y=py/(getHeight()-1.);if(!check){double[] raw=original.inverse(x,y);x=raw[0];y=raw[1];}sx+=x;sy+=y;count++;minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}
    public boolean onTouchEvent(android.view.MotionEvent e){
      if(!collecting||pendingTap)return true;
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
          if(++index==(check?5:42)){
            collecting=false;if(check){if(passed)thumbIntro();else review(false);return;}
            try{candidate=Calibration.fit(samples).withThumbOptimization(original.thumbOptimized);previewForThumb=false;message("正在应用临时结果，随后检查 5 个位置…");send("preview",candidate.encode(device));}
            catch(IllegalArgumentException ex){message(ex.getMessage()+"\n上一份结果未改变。","重新开始",()->{collecting=true;setContentView(new Targets(false));});}
          }else{pendingTap=false;invalidate();}
        },600);
      }else if(action==MotionEvent.ACTION_CANCEL){down=false;}
      return true;
    }
  }
}
