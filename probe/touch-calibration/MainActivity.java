package org.surface.touchcalibration;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.*;
import android.graphics.drawable.*;
import android.os.*;
import android.util.TypedValue;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

public class MainActivity extends Activity {
  File dir;String device="",nonce="",pending="",noiseNonce="",patch="",exportPayload="",exportNotice="";
  Calibration original,candidate;final Handler handler=new Handler(Looper.getMainLooper());final WizardFlow flow=new WizardFlow();
  boolean leaving=false,waiting=false,preview=false,noisePreview=false,collecting=false,noiseCapturing=false;
  boolean idleArmed=false,retesting=false,noiseBand=false,patchAvailable=false,exporting=false,thumbAfter=false,savedDone=false;
  boolean noiseDetected=false,noisePassed=false,noiseAuto=false;
  static final long NOISE_MS=45000;
  static final long SLIDE_MS=20000;
  boolean thumbRequested=false;
  boolean slideMode=false,slideAfter=false,slideEnough=false;SlideCapture slideCapture;
  String slideState="",slideRun="";

  // Fullscreen & tablet-mode state
  boolean enteringFullscreen=false,waitingForFullscreen=false,fullscreenAttempted=false,fullscreenFocusedAttempted=false;
  int fullscreenRetries=0;
  final Runnable clearEnteringFullscreen=()->enteringFullscreen=false;
  long retestAt=0;
  boolean retestCountdownArmed=false;
  final Runnable retestCountdown=new Runnable(){public void run(){
    if(!retestCountdownArmed||!noiseAuto||isFinishing())return;
    long left=retestAt-SystemClock.elapsedRealtime();
    if(left<=0){retestCountdownArmed=false;if(countdownRing!=null)countdownRing.update(0f,0);return;}
    long sec=(left+999)/1000;
    if(countdownRing!=null)countdownRing.update(left/5000f,(int)sec);
    handler.postDelayed(this,100);
  }};

  // Modern UI theme & dynamic screen state
  ThemeTokens ui;
  int uiStep=0; // 0: idle, 1: slide, 2: tap, 3: verify/calibrate, -1: system/fullscreen
  int uiIcon=ThemeTokens.ICON_IDLE;
  String uiTitle="",uiSubtitle="",uiTip="";
  boolean uiMetaSet=false;
  LinearLayout secondaryRow;
  int addedButtons=0;
  TextView status,liveSubStatus,liveBadge,holdHintView;
  CountdownRingView countdownRing;
  SwipeMonitorView activeSwipeView;
  Targets activeTargets;

  void setMeta(int step,int icon,String title,String subtitle,String tip){
    uiStep=step;uiIcon=icon;uiTitle=title;uiSubtitle=subtitle;uiTip=tip;uiMetaSet=true;
  }

  void slideIntro(){
    slideMode=true;noiseAuto=false;activeSwipeView=null;
    setMeta(1,ThemeTokens.ICON_SWIPE,
      slideAfter?"滑动复测确认":"单指滑动幽灵检测",
      slideAfter?"Fix applied · Verify swipe":"Step 2 of 4 · 20s single-finger swipe",
      "仅使用一根手指在屏幕中下部平稳来回滑动，避开顶部边缘；中途可抬手后继续。");
    LinearLayout l=layout((slideAfter?"已应用修复，重新检查。\n\nFix applied. Check again.\n":"滑动幽灵检测\n\nSwipe ghost-touch check\n")+"只用一根手指，在屏幕中下部来回滑动20秒。\n不要碰顶部，可抬手后继续。\nSwipe back and forth with ONE finger in the lower screen for 20 seconds.\nAvoid the top; lifting your finger is OK.");
    button(l,"开始滑动 / Swipe",()->{flow.changedSlide();send("noise-slide",noisePreview?"preview":"current");});
    button(l,"撤销退出 / Discard & exit",()->finish());
  }
  void slideResult(){
    activeSwipeView=null;
    slideEnough=slideCapture!=null&&slideCapture.enough()&&!slideState.equals("no_data")&&!slideState.equals("inconclusive")&&!slideState.equals("error")&&!slideState.equals("expired");
    record("slide-"+slideRun+".txt",slideState+"\n"+(slideCapture==null?"missing":slideCapture.summary())+"\nafter="+slideAfter+" operator_confirmed=false\n");
    if(!slideEnough){
      setMeta(1,ThemeTokens.ICON_WARN,"滑动数据不足","Retry single-finger swipe","请保持单指持续在中下部滑动，避免滑出顶部或中断。");
      message(slideState.equals("no_data")?"已记录触点，但原始数据不足，请重新检测。\nTouches recorded, but raw data is insufficient. Retry.":slideCapture!=null&&slideCapture.cancels>0?"滑动被中断，请重新检测。\nSwipe interrupted. Retry.":"尚未采到有效滑动，请单指来回滑动。\nNo valid swipe yet. Swipe with one finger.","重测 / Retry",this::slideIntro);
      return;
    }
    slideReview();
  }
  void slideReview(){
    noisePassed=slideEnough&&slideCapture.clean()&&slideState.equals("quiet");
    if(!noisePassed&&patchAvailable&&!slideAfter){
      setMeta(1,ThemeTokens.ICON_FIX,"检测到滑动伴随异常","Abnormal swipe detected","仅在确认刚才全程单指且未触碰顶部时，长按下方按钮应用修复。");
      LinearLayout l=layout("检测到滑动异常。确认刚才全程单指，且未触碰顶部后再应用修复。\nAbnormal swipe detected. Apply only if you used ONE finger and never touched the top.");
      holdButton(l,"长按应用 / Hold: apply",()->{record("slide-"+slideRun+"-confirmed.txt","single_finger=true no_top_contact=true\n");noiseAuto=true;send("noise-export","");});
      button(l,"撤销退出 / Discard & exit",()->finish());
      return;
    }
    setMeta(1,noisePassed?ThemeTokens.ICON_VERIFY:ThemeTokens.ICON_WARN,
      noisePassed?"滑动检测通过":"滑动仍有残留异常",
      noisePassed?"Single-finger swipe passed":"Anomalies remain in swipe test",
      noisePassed?"请确认刚才全程单指且未触碰顶部，长按按钮进入下一步单指点击检测。":"建议重测滑动；如确认当前体验可接受，也可长按继续。");
    LinearLayout l=layout(noisePassed?"滑动检测通过。\n请确认刚才全程单指，且未触碰顶部。\nSwipe passed. Confirm ONE finger was used and the top was not touched.":"仍有异常，未通过。\n请重测，或确认当前效果可以接受。\nNot passed: anomalies remain. Retest or explicitly accept the current result.");
    holdButton(l,noisePassed?"长按继续 / Hold: next":"长按接受 / Hold: accept",()->{
      record("slide-"+slideRun+"-confirmed.txt","single_finger=true no_top_contact=true\n");flow.slideRetested=true;flow.confirmSlide();
      if(noisePreview)send("noise-save","");else send("noise-resume","");
    });
    button(l,"重测滑动 / Retry swipe",this::slideIntro);
    if(!patch.isEmpty())button(l,"导出校准文件 / Export (.spkcal)…",this::exportPatch);
    button(l,"恢复 / Restore",()->send("noise-cancel",""));
  }
  Runnable noiseStep;
  void laterNoise(Runnable step){if(noiseStep!=null)handler.removeCallbacks(noiseStep);noiseStep=step;handler.postDelayed(step,5000);}
  long deadline,noiseDeadline,idleAt,noiseStart;int attempts=0,noiseContacts=0,beforeContacts=0,afterContacts=0,skippedRegions=0;
  String beforeNoise="",afterNoise="",plan="";
  ThumbTapCapture thumbBefore,thumbAfterData;Calibration thumbBase;
  boolean tapAfter=false,tapPreview=false,tapContinue=false,tapLearning=false;long tapDeadline;SingleTapCapture tapBefore;
  String read(String name)throws Exception{return new String(Files.readAllBytes(new File(dir,name).toPath()),StandardCharsets.UTF_8);}
  void record(String name,String body){try{Files.write(new File(dir,name).toPath(),body.getBytes(StandardCharsets.UTF_8));}catch(Exception ignored){}}
  void send(String command,String body){
    if(waiting)return;
    if(command.startsWith("noise-")&&noiseStep!=null)handler.removeCallbacks(noiseStep);
    if(command.equals("tap-begin"))tapLearning=true;
    try{
      if(command.equals("noise-learn")||command.equals("noise-retest")||command.equals("noise-slide")){
        retesting=command.equals("noise-retest");noiseCapturing=true;noiseContacts=0;noiseStart=SystemClock.uptimeMillis();
      }
      nonce=UUID.randomUUID().toString();File temp=new File(dir,"request.tmp");
      try(FileOutputStream f=new FileOutputStream(temp)){f.write((command+"\n"+nonce+"\n"+body).getBytes(StandardCharsets.UTF_8));f.getFD().sync();}
      if(!temp.renameTo(new File(dir,"request")))throw new IOException();
      pending=command;waiting=true;deadline=SystemClock.elapsedRealtime()+30000;handler.postDelayed(poll,300);
    }catch(Exception e){noiseCapturing=false;setMeta(-1,ThemeTokens.ICON_WARN,"连接失败","Connection failed","请关闭后重新打开触屏校准应用。");message("连接失败，请重新打开。\nConnection failed. Reopen the app.","关闭 / Close",()->finish());}
  }
  final Runnable poll=new Runnable(){public void run(){
    if(!waiting)return;
    try{
      String[] lines=read("reply").split("\n");
      if(lines.length>=2&&lines[0].equals(nonce)){
        waiting=false;String done=pending;pending="";
        if(done.equals("bundle-export")){
          if(leaving){handler.post(thisActivityCancel);return;}
          if(lines[1].equals("OK")){
            try{exportPayload=read("calibration-bundle");}catch(Exception e){exportPayload="";}
          }else{exportPayload=buildLocalBundleFallback();}
          if(exportPayload.trim().isEmpty()){exportNotice="暂无可导出的校准数据。\nNo calibration data to export.";returnToExportScreen();}
          else launchExportPicker();
          return;
        }
        if(!lines[1].equals("OK")){noiseCapturing=false;setMeta(-1,ThemeTokens.ICON_WARN,"操作未完成","Operation incomplete","服务返回异常，请重新打开应用再试。");message("操作未完成，请重新打开。\nOperation incomplete. Reopen the app.","关闭 / Close",()->finish());return;}
        if(leaving){
          if(done.equals("noise-learn")||done.equals("noise-retest")||done.equals("noise-slide"))noiseNonce=nonce;
          if(done.equals("noise-apply"))noisePreview=true;
          if(done.equals("preview"))preview=true;
          if(done.equals("tap-apply"))tapPreview=true;
          if(done.equals("tap-save")||done.equals("tap-cancel")){tapPreview=false;tapLearning=false;}
          if(done.equals("noise-save")||done.equals("noise-cancel")){noisePreview=false;noiseNonce="";}
          if(done.equals("save")||done.equals("cancel"))preview=false;
          handler.post(thisActivityCancel);return;
        }
        if(done.equals("tap-begin")||done.equals("tap-retest")){if(tapAfter)tapDeadline=SystemClock.elapsedRealtime()+590000;collecting=true;applyImmersiveFullscreen();setContentView(new PointView());return;}
        if(done.equals("tap-finish")){tapLearning=false;record("tap-analysis.txt",read("tap-status"));setMeta(2,ThemeTokens.ICON_LOADING,"正在应用点击优化","Adjusting tap profile…","");message("正在调整，请稍候…\nAdjusting…");send("tap-apply","");return;}
        if(done.equals("tap-apply")){tapPreview=true;tapAfter=true;tapDeadline=SystemClock.elapsedRealtime()+590000;pointIntro();return;}
        if(done.equals("tap-save")){tapPreview=false;flow.confirmTap();coordinateIntro();return;}
        if(done.equals("tap-cancel")){tapLearning=false;tapPreview=false;tapAfter=false;if(tapContinue){tapContinue=false;flow.useOriginalTap();coordinateIntro();}else pointIntro();return;}
        if(done.equals("noise-slide")){
          slideCapture=new SlideCapture();slideRun=nonce;noiseNonce=nonce;noiseStart=SystemClock.uptimeMillis();
          noiseDeadline=SystemClock.elapsedRealtime()+35000;noiseCapturing=true;
          showSwipeMonitor();handler.postDelayed(noisePoll,250);return;
        }
        if(done.equals("noise-learn")||done.equals("noise-retest")){
          noiseNonce=nonce;noiseDeadline=SystemClock.elapsedRealtime()+60000;
          showIdleMonitor();handler.postDelayed(noisePoll,250);return;
        }
        if(done.equals("noise-export")){patch=read("noise-patch");if(noiseAuto)send("noise-apply","");else patchReady();return;}
        if(done.equals("noise-apply")){noisePreview=true;if(slideMode){flow.changedSlide();slideAfter=true;slideIntro();}else{flow.changedNoise();prepareRetest();}return;}
        if(done.equals("noise-resume")){noiseNonce="";if(slideMode){flow.confirmSlide();pointIntro();}else{flow.confirmNoise();slideIntro();}return;}
        if(done.equals("noise-save")){noisePreview=false;noiseNonce="";if(slideMode){flow.confirmSlide();pointIntro();}else{flow.confirmNoise();slideIntro();}return;}
        if(done.equals("noise-cancel")){noiseAuto=false;noisePreview=false;flow.changedNoise();noiseIntro();return;}
        if(done.equals("preview")){preview=true;deadline=SystemClock.elapsedRealtime()+150000;if(thumbAfter)thumbIntro(true);else if(thumbRequested){thumbRequested=false;thumbIntro(false);}else review();return;}
        if(done.equals("save")){preview=false;savedDone=true;exportNotice="";calibrationSaved();return;}
        if(done.equals("cancel")||done.equals("skip")){preview=false;finish();return;}
      }
    }catch(Exception ignored){}
    if(SystemClock.elapsedRealtime()>deadline){waiting=false;noiseCapturing=false;setMeta(-1,ThemeTokens.ICON_WARN,"连接超时","Connection timed out","触摸服务响应超时，请关闭后重新打开应用。");message("连接超时，请重新打开。\nConnection timed out. Reopen the app.","关闭 / Close",()->finish());return;}
    handler.postDelayed(this,300);
  }};

  @Override public boolean dispatchTouchEvent(MotionEvent e){
    if(noiseCapturing&&slideMode&&slideCapture!=null){
      int a=e.getActionMasked(),n=e.getPointerCount();int[] ids=new int[n];double[] xs=new double[n],ys=new double[n];
      View v=getWindow().getDecorView();for(int i=0;i<n;i++){ids[i]=e.getPointerId(i);xs[i]=e.getX(i)/Math.max(1.,v.getWidth());ys[i]=e.getY(i)/Math.max(1.,v.getHeight());}
      slideCapture.frame(e.getEventTime(),ids,xs,ys,a==MotionEvent.ACTION_CANCEL);
      if(a==MotionEvent.ACTION_UP)slideCapture.frame(e.getEventTime(),new int[0],new double[0],new double[0],false);
      if(activeSwipeView!=null)activeSwipeView.onMotion(e);
      return true;
    }
    if(noiseCapturing){
      int a=e.getActionMasked();
      if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN){
        noiseContacts++;
        updateIdleContactBadge();
      }
      return true;
    }
    return super.dispatchTouchEvent(e);
  }

  // ---- Fullscreen & Tablet Mode Enforcement ----

  void applyImmersiveFullscreen(){
    Window w=getWindow();
    if(w==null)return;
    w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|WindowManager.LayoutParams.FLAG_FULLSCREEN);
    WindowManager.LayoutParams lp=w.getAttributes();
    if(Build.VERSION.SDK_INT>=30){
      lp.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
    }else if(Build.VERSION.SDK_INT>=28){
      lp.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
    }
    w.setAttributes(lp);
    View decor=w.getDecorView();
    if(Build.VERSION.SDK_INT>=30){
      w.setDecorFitsSystemWindows(false);
      w.setStatusBarColor(Color.TRANSPARENT);
      w.setNavigationBarColor(Color.TRANSPARENT);
      WindowInsetsController ctrl=w.getInsetsController();
      if(ctrl!=null){
        ctrl.hide(WindowInsets.Type.systemBars()|WindowInsets.Type.captionBar());
        ctrl.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
      }
    }else{
      decor.setSystemUiVisibility(5894);
    }
  }

  boolean hasActiveSession(){
    return waiting||noiseCapturing||collecting||preview||noisePreview||tapPreview||tapLearning;
  }

  void requestEnterFullscreen(boolean userInitiated){
    applyImmersiveFullscreen();
    if(Build.VERSION.SDK_INT<35||!isInMultiWindowMode())return;
    fullscreenAttempted=true;
    if(!hasActiveSession()){
      enteringFullscreen=true;
      handler.removeCallbacks(clearEnteringFullscreen);
      handler.postDelayed(clearEnteringFullscreen,1500);
    }
    try{
      requestFullscreenMode(FULLSCREEN_MODE_REQUEST_ENTER,new OutcomeReceiver<Void,Throwable>(){
        @Override public void onResult(Void result){
          handler.post(()->{
            enteringFullscreen=false;
            applyImmersiveFullscreen();
            if(waitingForFullscreen&&!isInMultiWindowMode()){
              waitingForFullscreen=false;
              load();
            }
          });
        }
        @Override public void onError(Throwable error){
          handler.post(()->{
            enteringFullscreen=false;
            if(userInitiated&&liveSubStatus!=null){
              liveSubStatus.setText("系统未自动切换全屏，请点击窗口顶部最大化/全屏按钮。\nPlease tap the window maximize/fullscreen button in the top bar.");
            }
          });
        }
      });
    }catch(Throwable ignored){
      enteringFullscreen=false;
    }
  }

  @Override public void onCreate(Bundle state){
    super.onCreate(state);
    ui=new ThemeTokens(this);
    dir=createDeviceProtectedStorageContext().getFilesDir();
    applyImmersiveFullscreen();
    if(isInMultiWindowMode())requestEnterFullscreen(false);
    setMeta(-1,ThemeTokens.ICON_LOADING,"正在连接本机触控服务","Loading touch calibration service…","请稍候，正在读取设备触控配置与传感器状态。");
    message("正在读取本机触摸服务…\nLoading touch service…");
    handler.postDelayed(this::load,450);
  }

  @Override public void onAttachedToWindow(){
    super.onAttachedToWindow();
    applyImmersiveFullscreen();
  }

  @Override public void onResume(){
    super.onResume();
    enteringFullscreen=false;
    applyImmersiveFullscreen();
    if(isInMultiWindowMode()&&!fullscreenAttempted)requestEnterFullscreen(false);
  }

  @Override public void onWindowFocusChanged(boolean hasFocus){
    super.onWindowFocusChanged(hasFocus);
    if(hasFocus){
      applyImmersiveFullscreen();
      if(isInMultiWindowMode()&&!fullscreenFocusedAttempted){
        fullscreenFocusedAttempted=true;
        requestEnterFullscreen(false);
      }
    }
  }

  @Override public void onMultiWindowModeChanged(boolean isInMultiWindowMode,Configuration newConfig){
    super.onMultiWindowModeChanged(isInMultiWindowMode,newConfig);
    applyImmersiveFullscreen();
    if(isInMultiWindowMode){
      fullscreenFocusedAttempted=false;
    }else if(waitingForFullscreen){
      waitingForFullscreen=false;
      handler.post(this::load);
    }
  }

  @Override public void onConfigurationChanged(Configuration newConfig){
    super.onConfigurationChanged(newConfig);
    ui=new ThemeTokens(this);
    applyImmersiveFullscreen();
  }

  void showFullscreenPrompt(){
    waitingForFullscreen=true;
    setMeta(-1,ThemeTokens.ICON_FULLSCREEN,"需要全屏模式进行校准","Full-screen mode required","触控校准需要覆盖整块物理屏幕以准确采集边缘与全屏坐标。点击下方按钮进入全屏后将自动开始。");
    LinearLayout l=layout("请全屏打开应用后校准。\nOpen full screen to calibrate.");
    if(Build.VERSION.SDK_INT>=35){
      button(l,"进入全屏并继续 / Enter Full Screen",()->requestEnterFullscreen(true));
    }
    button(l,"重新检测全屏 / Recheck",()->{applyImmersiveFullscreen();if(!isInMultiWindowMode()){waitingForFullscreen=false;load();}});
    button(l,"关闭 / Close",()->finish());
  }

  void load(){
    if(leaving||isFinishing())return;
    applyImmersiveFullscreen();
    try{
      String body=read("context");for(String l:body.split("\n"))if(l.startsWith("Device="))device=l.substring(7);
      if(!device.matches("[0-9a-f]{64}"))throw new IOException();original=new Calibration(body);
      if(isInMultiWindowMode()){
        if(Build.VERSION.SDK_INT>=35&&fullscreenRetries<6){
          fullscreenRetries++;
          requestEnterFullscreen(false);
          handler.postDelayed(this::load,250);
          return;
        }
        showFullscreenPrompt();
        return;
      }
      waitingForFullscreen=false;
      if(getIntent().getBooleanExtra("point_capture",false)){
        if(getIntent().getBooleanExtra("auto_capture",false))startPointCapture();else pointIntro();
      }else noiseIntro();
    }catch(Exception e){
      if(++attempts<30)handler.postDelayed(this::load,500);
      else{
        setMeta(-1,ThemeTokens.ICON_WARN,"触摸服务未就绪","Touch service not ready","未开始采集，请等待系统完成初始化后重试。");
        message("触摸服务未就绪，未开始采集。\nTouch service not ready. Capture has not started.","关闭 / Close",()->finish());
      }
    }
  }

  // ---- Modern Material You Card & Controls Builder ----

  void inferMetaIfUnset(String text){
    if(uiMetaSet){uiMetaSet=false;return;}
    if(text.startsWith("已生成修复方案")){
      uiStep=0;uiIcon=ThemeTokens.ICON_FIX;uiTitle="已生成静置修复方案";uiSubtitle="Static noise fix ready";
      uiTip="点击“应用复测”后将在 5 秒倒计时结束时自动开始 45 秒静置验证，也可先导出方案备份。";
    }else if(text.startsWith("已检测到异常")){
      uiStep=0;uiIcon=ThemeTokens.ICON_WARN;uiTitle="暂未生成可安全应用的方案";uiSubtitle="No safe candidate generated";
      uiTip="请确保设备平放稳定、手与触控笔远离屏幕后重新检测。";
    }else if(text.contains("正在准备修复方案")){
      uiStep=0;uiIcon=ThemeTokens.ICON_LOADING;uiTitle="正在生成修复方案";uiSubtitle="Preparing repair candidate…";uiTip="";
    }else if(text.contains("检测数据不足")){
      uiStep=0;uiIcon=ThemeTokens.ICON_WARN;uiTitle="检测数据不足";uiSubtitle="Insufficient samples";
      uiTip="请保持设备静置并重新进行 45 秒采样。";
    }else if(text.contains("45秒检测通过")){
      uiStep=0;uiIcon=ThemeTokens.ICON_VERIFY;uiTitle="45 秒静置检测通过";uiSubtitle="Idle check passed";uiTip="";
    }else{
      uiIcon=ThemeTokens.ICON_IDLE;uiTitle="触屏校准";uiSubtitle="Surface Touch Calibration";uiTip="";
    }
  }

  LinearLayout layout(String text){
    if(ui==null)ui=new ThemeTokens(this);
    applyImmersiveFullscreen();
    inferMetaIfUnset(text);
    activeSwipeView=null;
    countdownRing=null;
    liveSubStatus=null;
    liveBadge=null;
    holdHintView=null;
    secondaryRow=null;
    addedButtons=0;

    FrameLayout root=new FrameLayout(this);
    root.setBackgroundColor(ui.bg);

    LinearLayout page=new LinearLayout(this);
    page.setOrientation(LinearLayout.VERTICAL);
    page.setGravity(Gravity.CENTER_HORIZONTAL);
    page.setPadding(ui.dp(28),ui.dp(20),ui.dp(28),ui.dp(24));

    // Centered max-width shell for balanced landscape & portrait proportions
    LinearLayout shell=new LinearLayout(this);
    shell.setOrientation(LinearLayout.VERTICAL);
    shell.setGravity(Gravity.CENTER_HORIZONTAL);
    int screenW=getResources().getDisplayMetrics().widthPixels;
    int maxW=Math.min(screenW-ui.dp(48),ui.dp(720));
    LinearLayout.LayoutParams shellLp=new LinearLayout.LayoutParams(Math.max(ui.dp(320),maxW),LinearLayout.LayoutParams.WRAP_CONTENT);
    page.addView(shell,shellLp);

    // Top stepper bar (shown on all 4 calibration stages)
    if(uiStep>=0){
      StepperView stepper=new StepperView(this,ui,uiStep);
      LinearLayout.LayoutParams stLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,ui.dp(54));
      stLp.bottomMargin=ui.dp(14);
      shell.addView(stepper,stLp);
    }

    // Main Material You surface card
    LinearLayout card=new LinearLayout(this);
    card.setOrientation(LinearLayout.VERTICAL);
    card.setBackground(ui.roundedRectStroke(ui.card,ui.divider,1f,28f));
    card.setPadding(ui.dp(28),ui.dp(24),ui.dp(28),ui.dp(24));
    shell.addView(card,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));

    // Card header row: circular icon badge + title & subtitle
    LinearLayout header=new LinearLayout(this);
    header.setOrientation(LinearLayout.HORIZONTAL);
    header.setGravity(Gravity.CENTER_VERTICAL);
    card.addView(header,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));

    int badgeBg=uiIcon==ThemeTokens.ICON_WARN?ui.warnBg:uiIcon==ThemeTokens.ICON_VERIFY?ui.okBg:ui.primaryContainer;
    int badgeFg=uiIcon==ThemeTokens.ICON_WARN?ui.warnFg:uiIcon==ThemeTokens.ICON_VERIFY?ui.okFg:ui.onPrimaryContainer;
    FrameLayout iconBox=new FrameLayout(this);
    iconBox.setBackground(ui.roundedRect(badgeBg,16f));
    View glyph=ui.iconView(this,uiIcon,badgeFg,24);
    FrameLayout.LayoutParams gLp=new FrameLayout.LayoutParams(ui.dp(24),ui.dp(24),Gravity.CENTER);
    iconBox.addView(glyph,gLp);
    LinearLayout.LayoutParams ibLp=new LinearLayout.LayoutParams(ui.dp(48),ui.dp(48));
    ibLp.rightMargin=ui.dp(16);
    header.addView(iconBox,ibLp);

    LinearLayout titleCol=new LinearLayout(this);
    titleCol.setOrientation(LinearLayout.VERTICAL);
    header.addView(titleCol,new LinearLayout.LayoutParams(0,LinearLayout.LayoutParams.WRAP_CONTENT,1f));

    TextView titleView=new TextView(this);
    titleView.setText(uiTitle);
    titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP,21);
    titleView.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
    titleView.setTextColor(ui.textPrimary);
    titleCol.addView(titleView);

    if(uiSubtitle!=null&&!uiSubtitle.isEmpty()){
      TextView subView=new TextView(this);
      subView.setText(uiSubtitle);
      subView.setTextSize(TypedValue.COMPLEX_UNIT_SP,13);
      subView.setTextColor(ui.textSecondary);
      LinearLayout.LayoutParams subLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
      subLp.topMargin=ui.dp(2);
      titleCol.addView(subView,subLp);
    }

    // Subtle fullscreen indicator pill on top right of card
    TextView modePill=new TextView(this);
    modePill.setText(isInMultiWindowMode()?"窗口模式":"全屏校准");
    modePill.setTextSize(TypedValue.COMPLEX_UNIT_SP,11.5f);
    modePill.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
    modePill.setTextColor(isInMultiWindowMode()?ui.warnFg:ui.okFg);
    modePill.setBackground(ui.roundedRect(isInMultiWindowMode()?ui.warnBg:ui.okBg,999f));
    modePill.setPadding(ui.dp(12),ui.dp(5),ui.dp(12),ui.dp(5));
    header.addView(modePill);

    // Subtle divider
    View div=new View(this);
    div.setBackgroundColor(ui.divider);
    LinearLayout.LayoutParams divLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,Math.max(1,ui.dp(1)));
    divLp.topMargin=ui.dp(18);divLp.bottomMargin=ui.dp(16);
    card.addView(div,divLp);

    // Main message body
    status=new TextView(this);
    status.setText(formatBody(text));
    status.setTextSize(TypedValue.COMPLEX_UNIT_SP,15.5f);
    status.setLineSpacing(ui.dp(4),1.05f);
    status.setTextColor(ui.textPrimary);
    card.addView(status,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));

    // Optional tonal guidance tip box
    if(uiTip!=null&&!uiTip.isEmpty()){
      LinearLayout tipBox=new LinearLayout(this);
      tipBox.setOrientation(LinearLayout.HORIZONTAL);
      tipBox.setGravity(Gravity.CENTER_VERTICAL);
      tipBox.setBackground(ui.roundedRect(ui.cardAlt,16f));
      tipBox.setPadding(ui.dp(14),ui.dp(11),ui.dp(14),ui.dp(11));
      LinearLayout.LayoutParams tLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
      tLp.topMargin=ui.dp(14);
      card.addView(tipBox,tLp);

      View dot=new View(this);
      dot.setBackground(ui.roundedRect(ui.primary,999f));
      LinearLayout.LayoutParams dLp=new LinearLayout.LayoutParams(ui.dp(6),ui.dp(6));
      dLp.rightMargin=ui.dp(10);
      tipBox.addView(dot,dLp);

      TextView tipText=new TextView(this);
      tipText.setText(uiTip);
      tipText.setTextSize(TypedValue.COMPLEX_UNIT_SP,13.5f);
      tipText.setLineSpacing(ui.dp(2),1.05f);
      tipText.setTextColor(ui.textSecondary);
      tipBox.addView(tipText,new LinearLayout.LayoutParams(0,LinearLayout.LayoutParams.WRAP_CONTENT,1f));
    }

    // Dynamic live-status subtext slot (hidden until used)
    liveSubStatus=new TextView(this);
    liveSubStatus.setTextSize(TypedValue.COMPLEX_UNIT_SP,13);
    liveSubStatus.setTextColor(ui.textSecondary);
    liveSubStatus.setVisibility(View.GONE);
    LinearLayout.LayoutParams lsLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
    lsLp.topMargin=ui.dp(10);
    card.addView(liveSubStatus,lsLp);

    // Content & actions container returned to caller
    LinearLayout actions=new LinearLayout(this);
    actions.setOrientation(LinearLayout.VERTICAL);
    LinearLayout.LayoutParams aLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
    aLp.topMargin=ui.dp(18);
    card.addView(actions,aLp);

    ScrollView scroll=new ScrollView(this);
    scroll.setFillViewport(true);
    scroll.setVerticalScrollBarEnabled(false);
    LinearLayout wrap=new LinearLayout(this);
    wrap.setOrientation(LinearLayout.VERTICAL);
    wrap.setGravity(Gravity.CENTER);
    wrap.addView(page,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));
    scroll.addView(wrap,new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.WRAP_CONTENT));
    root.addView(scroll,new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.MATCH_PARENT));
    setContentView(root);
    root.post(this::applyImmersiveFullscreen);
    return actions;
  }

  String formatBody(String raw){
    if(raw==null)return "";
    return raw.replace("\n\n\n","\n\n").trim();
  }

  boolean isDestructiveLabel(String text){
    return text.contains("退出")||text.contains("关闭")||text.contains("撤销")||text.contains("放弃")||text.contains("恢复")||text.contains("Discard")||text.contains("Exit")||text.contains("Close")||text.contains("Restore");
  }

  void addActionView(LinearLayout container,View v,boolean fullWidth){
    if(fullWidth){
      secondaryRow=null;
      LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
      if(container.getChildCount()>0)lp.topMargin=ui.dp(10);
      container.addView(v,lp);
      return;
    }
    if(secondaryRow==null||secondaryRow.getChildCount()>=2){
      secondaryRow=new LinearLayout(this);
      secondaryRow.setOrientation(LinearLayout.HORIZONTAL);
      LinearLayout.LayoutParams rLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
      rLp.topMargin=ui.dp(10);
      container.addView(secondaryRow,rLp);
    }
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,LinearLayout.LayoutParams.WRAP_CONTENT,1f);
    if(secondaryRow.getChildCount()>0)lp.leftMargin=ui.dp(10);
    secondaryRow.addView(v,lp);
  }

  void button(LinearLayout l,String text,Runnable action){
    boolean isPrimary=addedButtons==0&&!isDestructiveLabel(text);
    boolean isDestructive=isDestructiveLabel(text);
    addedButtons++;

    TextView b=new TextView(this);
    b.setText(text);
    b.setGravity(Gravity.CENTER);
    b.setAllCaps(false);
    b.setTextSize(TypedValue.COMPLEX_UNIT_SP,isPrimary?15.5f:14.5f);
    b.setTypeface(Typeface.create("sans-serif-medium",isPrimary?Typeface.BOLD:Typeface.NORMAL));
    b.setMinHeight(ui.dp(isPrimary?52:46));
    b.setPadding(ui.dp(18),ui.dp(11),ui.dp(18),ui.dp(11));
    b.setClickable(true);
    b.setFocusable(true);

    if(isPrimary){
      b.setTextColor(ui.onPrimary);
      b.setBackground(ui.pillRipple(ui.primary,0));
    }else if(isDestructive){
      b.setTextColor(ui.textSecondary);
      b.setBackground(ui.pillRipple(Color.TRANSPARENT,ui.divider));
    }else{
      b.setTextColor(ui.onPrimaryContainer);
      b.setBackground(ui.pillRipple(ui.cardAlt,ui.divider));
    }
    b.setOnClickListener(v->{if(!waiting)action.run();});
    addActionView(l,b,isPrimary);
  }

  void holdButton(LinearLayout l,String text,Runnable action){
    addedButtons++;
    HoldPillButton hb=new HoldPillButton(this,ui,text,()->{if(!waiting)action.run();},()->{
      if(holdHintView!=null){
        holdHintView.setText("请按住按钮约 0.7 秒以确认操作 · Press and hold for 0.7s to confirm");
        holdHintView.setVisibility(View.VISIBLE);
      }
    });
    addActionView(l,hb,true);
    holdHintView=new TextView(this);
    holdHintView.setText("长按上方按钮确认，防止误触 · Hold to prevent accidental taps");
    holdHintView.setTextSize(TypedValue.COMPLEX_UNIT_SP,12);
    holdHintView.setTextColor(ui.textSecondary);
    holdHintView.setGravity(Gravity.CENTER);
    LinearLayout.LayoutParams hLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
    hLp.topMargin=ui.dp(6);
    l.addView(holdHintView,hLp);
  }

  void drawCue(Canvas c,String text,float x,float y,Paint paint){
    float old=paint.getTextSize();
    float width=Math.max(1,c.getWidth()-48);
    // Split bilingual cues into separate lines; fit each line without clipping.
    String[] lines=text.split(" \\| |\\n",-1);
    float size=old;
    for(String line:lines){float measured=paint.measureText(line);if(measured>width)size=Math.min(size,old*width/measured);}
    paint.setTextSize(size);
    for(int i=0;i<lines.length;i++)c.drawText(lines[i],x,y+i*size*1.25f,paint);
    paint.setTextSize(old);
  }

  void message(String text){layout(text);}
  void message(String text,String label,Runnable action){LinearLayout l=layout(text);button(l,label,action);}

  // ---- Stage 1: Idle Noise Check ----

  void showIdleMonitor(){
    setMeta(0,ThemeTokens.ICON_IDLE,
      retesting?"静置复测进行中":"静置基准检测进行中",
      retesting?"45s post-fix idle verification":"Step 1 of 4 · 45s static noise scan",
      "检测期间请保持设备静止，切勿触碰屏幕、边框或放置触控笔。");
    LinearLayout l=layout("正在检测，请勿触摸。剩余 45 秒。\nChecking. Do NOT touch. 45s left.");
    countdownRing=new CountdownRingView(this,ui,45,"剩余秒数");
    LinearLayout.LayoutParams rLp=new LinearLayout.LayoutParams(ui.dp(156),ui.dp(156));
    rLp.gravity=Gravity.CENTER_HORIZONTAL;
    rLp.topMargin=ui.dp(4);rLp.bottomMargin=ui.dp(12);
    l.addView(countdownRing,rLp);

    liveBadge=new TextView(this);
    liveBadge.setTextSize(TypedValue.COMPLEX_UNIT_SP,13);
    liveBadge.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
    liveBadge.setPadding(ui.dp(16),ui.dp(7),ui.dp(16),ui.dp(7));
    LinearLayout.LayoutParams bLp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,LinearLayout.LayoutParams.WRAP_CONTENT);
    bLp.gravity=Gravity.CENTER_HORIZONTAL;
    l.addView(liveBadge,bLp);
    updateIdleContactBadge();
  }

  void updateIdleContactBadge(){
    if(liveBadge==null)return;
    if(noiseContacts>0||noiseDetected){
      liveBadge.setText("已检测到异常触点 / 窗口触点: "+noiseContacts+" · Unexpected contacts: "+noiseContacts);
      liveBadge.setTextColor(ui.warnFg);
      liveBadge.setBackground(ui.roundedRect(ui.warnBg,999f));
      if(countdownRing!=null)countdownRing.setWarn(true);
    }else{
      liveBadge.setText("窗口触点: 0 · 画面静置中 / Clean idle");
      liveBadge.setTextColor(ui.okFg);
      liveBadge.setBackground(ui.roundedRect(ui.okBg,999f));
      if(countdownRing!=null)countdownRing.setWarn(false);
    }
  }

  void showSwipeMonitor(){
    applyImmersiveFullscreen();
    activeSwipeView=new SwipeMonitorView(this,ui);
    setContentView(activeSwipeView);
  }

  void noiseIntro(){
    slideMode=slideAfter=false;slideCapture=null;noiseAuto=false;activeSwipeView=null;
    flow.changedNoise();patchAvailable=false;patch="";noiseBand=false;exportNotice="";
    setMeta(0,ThemeTokens.ICON_IDLE,"准备开始静置检测","Step 1 of 4 · Static noise baseline","请将设备平放稳固，双手与触控笔完全离开屏幕。倒计时结束后自动开始 45 秒静置采样。");
    LinearLayout l=layout("请放稳设备，移开手与触控笔。\n5 秒后自动开始 45 秒静置检测，请保持静置。\nPlace the device securely and remove hands and pen.\nStarts automatically in 5 seconds; keep still.");
    countdownRing=new CountdownRingView(this,ui,5,"秒后开始");
    LinearLayout.LayoutParams rLp=new LinearLayout.LayoutParams(ui.dp(120),ui.dp(120));
    rLp.gravity=Gravity.CENTER_HORIZONTAL;
    rLp.bottomMargin=ui.dp(10);
    l.addView(countdownRing,rLp);
    button(l,"立即开始 / Start",()->{cancelCountdown();send("noise-learn","");});
    button(l,"退出 / Exit",()->{cancelCountdown();finish();});
    idleArmed=true;idleAt=SystemClock.elapsedRealtime()+5000;handler.post(idleCountdown);
  }
  void cancelCountdown(){idleArmed=false;handler.removeCallbacks(idleCountdown);retestCountdownArmed=false;handler.removeCallbacks(retestCountdown);}
  final Runnable idleCountdown=new Runnable(){public void run(){
    if(!idleArmed||isFinishing())return;long left=idleAt-SystemClock.elapsedRealtime();
    if(left<=0){idleArmed=false;send("noise-learn","");return;}
    long sec=(left+999)/1000;
    status.setText("请放稳设备，移开手与触控笔。\n将在 "+sec+" 秒后自动开始静置检测。\nKeep device still; starting in "+sec+"s.");
    if(countdownRing!=null)countdownRing.update(left/5000f,(int)sec);
    handler.postDelayed(this,100);
  }};
  Map<String,String> details(String[] lines){Map<String,String> m=new HashMap<>();for(int i=1;i<lines.length;i++)for(String w:lines[i].trim().split("\\s+")){int k=w.indexOf('=');if(k>0)m.put(w.substring(0,k),w.substring(k+1));}return m;}
  String noiseSummary(String state,String frames,Map<String,String> info){
    return "Raw signal: "+state+" | frames: "+frames+" | abnormal_frames: "+info.getOrDefault("abnormal_frames","unknown")
      +"\nWindow touches: "+noiseContacts+" | observed: "+String.format(Locale.ROOT,"%.1fs",(SystemClock.uptimeMillis()-noiseStart)/1000.)
      +"\nElectrode rows: "+info.getOrDefault("row_ids","none")+" | columns: "+info.getOrDefault("column_ids","none")+" | power: "+info.getOrDefault("power_online","unknown");
  }
  final Runnable noisePoll=new Runnable(){public void run(){
    if(SystemClock.elapsedRealtime()>noiseDeadline){
      noiseCapturing=false;activeSwipeView=null;
      setMeta(-1,ThemeTokens.ICON_WARN,"检测超时","Check timed out","服务响应超时，请关闭后重新打开应用。");
      message("检测超时，请重新打开。\nCheck timed out. Reopen the app.","关闭 / Close",()->finish());return;
    }
    try{
      String[] lines=read("noise-status").trim().split("\n"),f=lines[0].split("\\s+");
      if(f.length==4&&f[0].equals(noiseNonce)){
        Map<String,String> info=details(lines);
        if(slideMode){
          if(!f[1].equals("learning")){
            noiseCapturing=false;slideState=f[1];noiseBand=info.getOrDefault("candidate_kind","none").equals("band");
            patchAvailable=f[1].equals("line_noise")&&noiseBand;slideResult();return;
          }
          long elapsed=SystemClock.uptimeMillis()-noiseStart;
          long remMs=Math.max(0,SLIDE_MS-elapsed);
          long left=(remMs+999)/1000;
          boolean multi=slideCapture!=null&&slideCapture.multiFrames>0;
          if(activeSwipeView!=null)activeSwipeView.updateState(remMs/(float)SLIDE_MS,(int)left,multi,slideCapture!=null?slideCapture.moves:0);
          handler.postDelayed(this,200);return;
        }
        boolean abnormal=!info.getOrDefault("abnormal_frames","0").equals("0")||noiseContacts>0;
        noiseDetected=abnormal||Arrays.asList("ready","line_noise","broad_signal","transient","unresolved").contains(f[1]);
        if(!f[1].equals("learning")){noiseCapturing=false;String summary=noiseSummary(f[1],f[2],info);
          noisePassed=WizardFlow.idleOutputClean(f[1],Integer.parseInt(f[2]),noiseContacts);
          noiseBand=info.getOrDefault("candidate_kind","none").equals("band");
          patchAvailable=f[1].equals("ready")||f[1].equals("line_noise")&&noiseBand;
          if(retesting){afterContacts=noiseContacts;afterNoise=summary;flow.finishRetest();record("idle-after.txt",summary);noiseReview();}
          else{beforeContacts=noiseContacts;beforeNoise=summary;record("idle-before.txt",summary);noiseBand=info.getOrDefault("candidate_kind","none").equals("band");patchAvailable=f[1].equals("ready")||f[1].equals("line_noise")&&noiseBand;detectionResult();}
          return;
        }
        long elapsed=SystemClock.uptimeMillis()-noiseStart;
        long remMs=Math.max(0,NOISE_MS-elapsed);
        long left=(remMs+999)/1000;
        if(countdownRing!=null)countdownRing.update(remMs/(float)NOISE_MS,(int)left);
        updateIdleContactBadge();
        status.setText((noiseDetected?"已检测到异常触点。\nUnexpected touches detected.\n\n":"")+"正在检测，请勿触摸。剩余 "+left+" 秒。\nChecking. Do NOT touch. "+left+"s remaining.");
      }
    }catch(Exception ignored){}handler.postDelayed(this,250);
  }};

  void detectionResult(){
    if(noisePassed){flow.finishRetest();noiseReview();return;}
    if(patchAvailable){
      noiseAuto=false;message("正在准备修复方案…\nPreparing fix…");send("noise-export","");return;
    }
    LinearLayout l=layout(noiseDetected?"已检测到异常，暂未生成可安全应用的方案。\nAnomalies detected. No safe fix available yet.":noisePassed?"45秒检测通过，请复测确认。\n45-second check passed. Retest to confirm.":"检测数据不足，请重新检测。\nInsufficient data. Retry.");
    button(l,"继续复测 / Retest",this::prepareRetest);
    button(l,"重新开始 / Restart",this::noiseIntro);button(l,"退出 / Exit",()->finish());
  }
  void patchReady(){
    LinearLayout l=layout("已生成修复方案。\nFix ready."+(exportNotice.isEmpty()?"":"\n\n"+exportNotice));
    button(l,"应用复测 / Apply & test",()->{noiseAuto=true;send("noise-apply","");});
    button(l,"导出校准文件 / Export (.spkcal)…",this::exportPatch);
    button(l,"放弃重测 / Discard & retry",this::noiseIntro);
  }
  void exportPatch(){
    if(waiting)return;
    send("bundle-export","");
  }
  String buildLocalBundleFallback(){
    StringBuilder sb=new StringBuilder();
    boolean hasAny=false;
    sb.append("SPXTOUCH1\nDevice=").append(device).append("\nNoiseDisabled=0\n");
    if(savedDone&&candidate!=null&&device.matches("[0-9a-f]{64}")){
      String prof=candidate.encode(device);
      if(!prof.isEmpty()){sb.append("[Profile]\n").append(prof);if(!prof.endsWith("\n"))sb.append('\n');sb.append("[/Profile]\n");hasAny=true;}
    }
    if(!patch.isEmpty()){
      sb.append("[Noise]\n").append(patch);if(!patch.endsWith("\n"))sb.append('\n');sb.append("[/Noise]\n");hasAny=true;
    }
    return hasAny?sb.toString():"";
  }
  void launchExportPicker(){
    if(exportPayload.isEmpty())return;
    String prefix=device!=null&&device.matches("[0-9a-f]{8,64}")?"surface-touch-"+device.substring(0,8):"surface-touch";
    try{exporting=true;Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("application/octet-stream");i.putExtra(Intent.EXTRA_TITLE,prefix+".spkcal");startActivityForResult(i,41);}
    catch(Exception e){exporting=false;exportNotice="暂时无法导出，请重试。\nCannot export now. Retry.";returnToExportScreen();}
  }
  void returnToExportScreen(){
    if(savedDone)calibrationSaved();
    else if(slideMode)slideReview();
    else if(retesting&&flow.retested)noiseReview();
    else patchReady();
  }
  void calibrationSaved(){
    setMeta(3,ThemeTokens.ICON_VERIFY,"校准已保存","Calibration saved","所有本机触控参数已写入并生效。");
    LinearLayout l=layout("已保存。\nSaved."+(exportNotice.isEmpty()?"":"\n\n"+exportNotice));
    button(l,"导出校准文件 / Export (.spkcal)…",this::exportPatch);
    button(l,"完成 / Done",()->finish());
  }
  @Override protected void onActivityResult(int request,int result,Intent data){
    super.onActivityResult(request,result,data);if(request!=41)return;exporting=false;
    applyImmersiveFullscreen();
    if(result==RESULT_OK&&data!=null&&data.getData()!=null){
      try(OutputStream o=getContentResolver().openOutputStream(data.getData(),"w")){if(o==null)throw new IOException();o.write(exportPayload.getBytes(StandardCharsets.UTF_8));o.flush();exportNotice="校准文件已保存（.spkcal）。\nCalibration file saved (.spkcal).";}
      catch(Exception e){exportNotice="保存失败，请重试。\nSave failed. Retry.";}
    }else exportNotice="已取消文件导出。\nExport cancelled.";returnToExportScreen();
  }
  void prepareRetest(){
    retestCountdownArmed=false;handler.removeCallbacks(retestCountdown);
    setMeta(0,ThemeTokens.ICON_IDLE,
      noisePreview?"准备静置复测":"准备 45 秒静置复测",
      noisePreview?"Fix applied · Verifying result":"Static noise retest",
      noiseAuto?"修复方案已临时生效，5 秒后将自动开始 45 秒复测，请保持双手离开屏幕。":"请确保设备放稳且无人触碰，准备好后点击开始复测。");
    LinearLayout l=layout((noisePreview?"已应用修复，重新检查。\nFix applied. Check again.\n\n":"")+"请放稳设备，移开手与触控笔。\nKeep device still and remove hands and pen.\n"+(noiseAuto?"5 秒后自动开始 45 秒复测。\n45-second retest starts in 5 seconds.":"准备好后开始 45 秒复测。\nStart the 45-second retest when ready."));
    if(noiseAuto){
      countdownRing=new CountdownRingView(this,ui,5,"秒后复测");
      LinearLayout.LayoutParams rLp=new LinearLayout.LayoutParams(ui.dp(116),ui.dp(116));
      rLp.gravity=Gravity.CENTER_HORIZONTAL;rLp.bottomMargin=ui.dp(10);
      l.addView(countdownRing,rLp);
      retestAt=SystemClock.elapsedRealtime()+5000;
      retestCountdownArmed=true;
      handler.post(retestCountdown);
    }
    button(l,"开始复测 / Retest",()->{noiseAuto=false;retestCountdownArmed=false;handler.removeCallbacks(retestCountdown);send("noise-retest",noisePreview?"preview":"current");});
    button(l,"恢复 / Restore",()->{noiseAuto=false;retestCountdownArmed=false;handler.removeCallbacks(retestCountdown);send("noise-cancel","");});
    if(noiseAuto)laterNoise(()->{if(noiseAuto&&!leaving&&!waiting){noiseAuto=false;retestCountdownArmed=false;send("noise-retest","preview");}});
  }
  void noiseReview(){
    noiseAuto=false;
    setMeta(0,noisePassed?ThemeTokens.ICON_VERIFY:ThemeTokens.ICON_WARN,
      noisePassed?"静置检测已通过":"静置检测仍有异常",
      noisePassed?"No unexpected touches recorded":"Residual anomalies detected",
      noisePassed?"确认静置期间未触屏，再长按下方按钮进入下一步滑动检测。":"可手动再修复一次并复测，或恢复原有规则重新开始。");
    LinearLayout l=layout(noisePassed?"本轮未记录到意外触点。\nNo unexpected touches recorded.\n\n"+(noiseDetected?"仍有底层信号残留，接着检查正常单指滑动。\nSignal anomalies remain; check normal swiping next.":"接着进行 20 秒单指滑动检查。\nCheck single-finger swiping next."):noiseDetected?"本轮仍有异常。可手动再修复一次，或恢复原规则。\nAnomalies remain. Manually apply another fix or restore rules.":"数据不足，请重新检测。\nInsufficient data. Retry.");
    if(noisePassed){
      holdButton(l,"长按继续 / Hold: next",()->{if(!flow.retested)return;if(noisePreview){flow.confirmNoise();slideIntro();}else send("noise-resume","");});
    }
    if(!noisePassed&&patchAvailable)button(l,"再次修复 / Fix again",()->{noiseAuto=true;send("noise-export","");});
    if(!patch.isEmpty())button(l,"导出校准文件 / Export (.spkcal)…",this::exportPatch);
    button(l,"重测静置 / Retry idle",this::prepareRetest);
    button(l,"恢复重来 / Restore & restart",()->send("noise-cancel",""));
  }

  // ---- Stage 3: 15-Point Single-Finger Tap Check ----

  void pointIntro(){
    setMeta(2,ThemeTokens.ICON_TAP,
      tapAfter?"单指点击复测":"单指点击一致性检测",
      tapAfter?"Verify tap optimization · 15 targets":"Step 3 of 4 · 15 single-finger taps",
      "用一根手指轻点屏幕上的黄色十字靶心后立刻抬手；靶心变绿并等待 1 秒后将自动切换下一点。");
    LinearLayout l=layout((tapAfter?"再测一次 / Retest\n":"单指点击检测 / Single-finger tap check\n")+"用一根手指轻点黄色十字，然后抬手，共 15 次。\n采到后等待 1 秒，再点击下一个靶心。\n\nTap the yellow cross with ONE finger, then lift (15 taps).\nWait 1 second after capture before the next target.");
    button(l,"开始点击检测 / Start",this::startPointCapture);
    button(l,"退出 / Exit",()->finish());
  }
  void startPointCapture(){if(tapAfter)send("tap-retest","");else{flow.changedTap();send("tap-begin","");}}
  boolean tapAlive(){return !tapAfter||tapPreview&&SystemClock.elapsedRealtime()<tapDeadline;}

  final class PointView extends View {
    final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);final String run=UUID.randomUUID().toString();
    final SingleTapCapture test=new SingleTapCapture(SystemClock.uptimeMillis()+3000,0x535058L+device.hashCode());
    final StringBuilder events=new StringBuilder();boolean written=false,truncated=false;int lastCue=-2;
    float lastTouchX=-1,lastTouchY=-1;long lastTouchAt=0;
    PointView(){super(MainActivity.this);record("point15-active-run.txt",run+"\n");}
    void logCue(int i,long now){try{JSONObject j=new JSONObject();j.put("kind","cue").put("run",run).put("time_ms",now).put("trial",i);if(i>=0&&i<15)j.put("target",new JSONArray(test.targets[i]));events.append(j).append('\n');}catch(Exception ignored){}}
    void storeResult(boolean complete){
      if(written)return;written=true;test.complete=complete;
      try{JSONObject j=new JSONObject();j.put("run",run).put("device",device).put("start_ms",test.start).put("advance_mode","captured_then_1000ms_and_released").put("after_capture_ms",1000).put("completed_trials",test.completed()).put("shown_at_ms",new JSONArray(test.shownAt)).put("captured_at_ms",new JSONArray(test.capturedAt)).put("ended_at_ms",new JSONArray(test.endedAt)).put("complete",complete).put("truncated",truncated);
        j.put("starts",new JSONArray(test.starts)).put("peak_simultaneous",new JSONArray(test.peak)).put("gesture_groups",new JSONArray(test.groups)).put("cancels",new JSONArray(test.cancels)).put("targets",new JSONArray(test.targets));
        j.put("operator_confirmed",false);record("point15-summary.json",j.toString(2));record("point15-events.jsonl",events.toString());record("point15-context.profile",original.encode(device));record(tapAfter?"tap-after.json":"tap-before.json",j.toString(2));
      }catch(Exception e){setMeta(2,ThemeTokens.ICON_WARN,"保存失败","Save failed","");message("保存失败，请重新开始。\nSave failed. Start again.","关闭 / Close",()->finish());}
    }
    final Runnable tick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;long now=SystemClock.uptimeMillis();test.tick(now);int i=test.trial(now);
      if(!tapAlive()){collecting=false;storeResult(false);tapContinue=false;send("tap-cancel","");return;}
      if(i!=lastCue){lastCue=i;logCue(i,now);}
      if(test.finished(now)){storeResult(true);collecting=false;if(truncated){setMeta(2,ThemeTokens.ICON_WARN,"记录未完整保存","Recording incomplete","");message("记录未完整保存，请重新开始。\nRecording incomplete. Start again.","重试 / Retry",MainActivity.this::startPointCapture);}else pointResult(test,run);return;}invalidate();handler.postDelayed(this,33);}};
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(tick);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(tick);storeResult(test.finished(SystemClock.uptimeMillis()));super.onDetachedFromWindow();}
    protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density;
      drawCanvasBackdrop(c,w,h,paint);
      long now=SystemClock.uptimeMillis();int i=test.trial(now);

      // Position floating HUD away from active target so they never collide
      float hudCenterY=h*0.20f;
      if(i>=0&&i<15&&test.targets[i][1]<0.35)hudCenterY=h*0.76f;
      float progress=i<0?0f:(i+(test.captured()?0.5f:0f))/15f;
      String titleText=i<0?"准备开始 · 第 1 / 15 点":"单指点击检测 · "+(i+1)+" / 15";
      String cueText;
      if(i<0){
        long sec=(test.start-now+999)/1000;
        cueText="准备中，"+sec+" 秒后出现靶心 · Ready in "+sec+"s";
      }else if(i>=15){
        return;
      }else{
        boolean tap=!test.captured();
        cueText=tap?"轻点黄色靶心，然后立刻抬手 · Tap yellow target, then lift"
          :test.remaining(now)>0?String.format(Locale.ROOT,"已采集，%1$.1f 秒后进入下一点 · Next in %1$.1fs",test.remaining(now)/1000.)
          :"已满 1 秒，请完全抬手继续 · Lift finger fully to continue";
      }
      drawFloatingHud(c,w,hudCenterY,titleText,cueText,progress,i>=0&&test.captured()?ui.targetGreen:ui.targetAmber,paint,d);

      if(lastTouchX>=0&&now-lastTouchAt<450){
        float frac=(now-lastTouchAt)/450f;
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2f*d);
        paint.setColor(Color.argb((int)((1f-frac)*160),125,211,252));
        c.drawCircle(lastTouchX,lastTouchY,ui.dp(12)+frac*ui.dp(36),paint);
      }
      if(i<0)return;
      boolean tap=!test.captured();
      double[] t=test.targets[i];float x=(float)t[0]*(w-1),y=(float)t[1]*(h-1);
      float holdFrac=tap?0f:1f-Math.min(1f,test.remaining(now)/1000f);
      drawPrecisionTarget(c,x,y,!tap,holdFrac,now,paint,d);
    }
    public boolean onTouchEvent(MotionEvent e){
      if(!collecting)return true;
      int idx=e.getActionIndex();
      if(e.getActionMasked()==MotionEvent.ACTION_DOWN||e.getActionMasked()==MotionEvent.ACTION_POINTER_DOWN){
        lastTouchX=e.getX(idx);lastTouchY=e.getY(idx);lastTouchAt=SystemClock.uptimeMillis();
      }
      test.event(e.getActionMasked(),e.getPointerCount(),e.getEventTime());
      if(events.length()<2000000){try{JSONObject j=new JSONObject();j.put("kind","event").put("time_ms",e.getEventTime()).put("down_ms",e.getDownTime()).put("trial",test.trial(e.getEventTime())).put("action",e.getActionMasked()).put("action_index",e.getActionIndex());JSONArray pp=new JSONArray();
        for(int i=0;i<e.getPointerCount();i++){JSONObject p=new JSONObject();p.put("id",e.getPointerId(i)).put("x",e.getX(i)/Math.max(1,getWidth()-1)).put("y",e.getY(i)/Math.max(1,getHeight()-1)).put("major_px",e.getTouchMajor(i)).put("minor_px",e.getTouchMinor(i)).put("pressure",e.getPressure(i)).put("tool",e.getToolType(i));pp.put(p);}j.put("pointers",pp);events.append(j).append('\n');
      }catch(Exception ignored){}}else truncated=true;return true;
    }
  }

  void pointResult(SingleTapCapture t,String run){
    if(!tapAfter){
      tapBefore=t;
      boolean isClean=t.clean();
      setMeta(2,isClean?ThemeTokens.ICON_VERIFY:ThemeTokens.ICON_FIX,
        isClean?"15 次点击检测通过":"15 次点击采集完成",
        isClean?"All 15 taps clean · Confirm to continue":"Tap anomalies found · Confirm to optimize",
        isClean?"未发现连点或断触，确认刚才每个十字都只点了一次后长按继续。":"检测到重复或分裂触点，确认刚才每个十字都只点了一次后长按生成优化方案。");
      LinearLayout l=layout("点击完成\n刚才是否每个十字都只点了一次？\nTaps complete. Did you tap EACH cross exactly ONCE?");
      holdButton(l,isClean?"长按确认 / Hold: confirm":"长按调整 / Hold: adjust",()->{record("point15-operator-confirmed.txt",run+"\nSINGLE_FINGER_ONE_TAP_PER_TARGET\n");if(t.clean()){tapContinue=true;send("tap-cancel","");}else{setMeta(2,ThemeTokens.ICON_LOADING,"正在分析点击数据","Analyzing tap evidence…","");message("正在分析，请稍候…\nAnalyzing…");send("tap-finish","");}});
      button(l,"重采 / Recapture",this::pointIntro);return;
    }
    flow.tapRetested=true;
    boolean improved=tapBefore!=null&&t.total()<=tapBefore.total()&&t.multiTrials()<=tapBefore.multiTrials();
    setMeta(2,improved?ThemeTokens.ICON_VERIFY:ThemeTokens.ICON_WARN,
      "点击复测完成",
      improved?"Tap behavior improved · Verify below":"Retest complete · Check gestures",
      "请在下方交互测试区尝试轻点、快速双击、拖动和双指缩放，确认体验正常后再长按保存。");
    LinearLayout l=layout("复测完成\n请轻点、快速双击、拖动和双指缩放，确认操作正常。\nRetest complete. Check taps, double taps, dragging and pinch zoom.");
    addGesturePad(l);
    if(improved)
      holdButton(l,"长按保存 / Hold: save",()->{if(tapAlive()){record("tap-confirmed.txt",run+"\nUSER_ACCEPTED\n");send("tap-save","");}});
    button(l,"复测 / Retest",this::startPointCapture);
    button(l,"恢复继续 / Restore & next",()->{tapContinue=true;send("tap-cancel","");});
  }

  void addGesturePad(LinearLayout l){
    GesturePadView pad=new GesturePadView(this,ui);
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,ui.dp(196));
    lp.bottomMargin=ui.dp(6);
    l.addView(pad,lp);
  }

  // ---- Stage 4: Gesture Verification, Coordinate Calibration & Thumb Check ----

  void coordinateIntro(){
    if(!flow.noiseConfirmed){prepareRetest();return;}if(!flow.slideConfirmed){slideIntro();return;}if(!flow.tapConfirmed){pointIntro();return;}
    setMeta(3,ThemeTokens.ICON_VERIFY,"最后一步：手势验证与可选校准","Step 4 of 4 · Verify touch & optional calibration","在下方测试区试用轻点、双击、拖动与双指缩放。若落点准确即可长按完成；有边缘偏移再选“坐标校准”，拇指易断触可选“拇指检测”。");
    LinearLayout l=layout("请轻点、快速双击、拖动和双指缩放。\n位置准确即可完成；有偏移再校准，拇指异常可单独检测。\nCheck taps, double taps, dragging and pinch zoom.\nFinish if accurate; calibrate only for offset. Thumb check is optional.");
    addGesturePad(l);
    holdButton(l,"长按完成 / Hold: done",()->finish());
    button(l,"坐标校准 / Calibrate",this::launchCoordinateTargets);
    button(l,"拇指检测 / Thumb test",()->{candidate=original;thumbAfter=false;thumbRequested=true;send("preview",candidate.encode(device));});
  }

  void launchCoordinateTargets(){
    setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    applyImmersiveFullscreen();
    View decor=getWindow().getDecorView();
    if(decor.getHeight()>decor.getWidth()){
      setMeta(3,ThemeTokens.ICON_FULLSCREEN,"请横屏持握设备","Hold device in landscape","坐标校准需要与屏幕原生横屏坐标系对齐，请将平板横置后点击开始。");
      LinearLayout l=layout("请将设备横屏放置后再开始全屏坐标校准。\nPlease hold the device in landscape before starting coordinate calibration.");
      button(l,"开始坐标校准 / Start",()->{skippedRegions=0;collecting=true;applyImmersiveFullscreen();setContentView(new Targets());});
      button(l,"返回 / Back",this::coordinateIntro);
      return;
    }
    skippedRegions=0;collecting=true;setContentView(new Targets());
  }

  boolean trialAlive(){if(preview&&SystemClock.elapsedRealtime()<deadline)return true;collecting=false;send("cancel","");setMeta(3,ThemeTokens.ICON_WARN,"预览已超时","Preview timed out","正在自动恢复上次保存的坐标参数。");message("预览已超时，正在恢复上次保存的坐标结果。\nPreview timed out. Restoring saved coordinates.");return false;}

  void thumbIntro(boolean after){
    collecting=false;thumbAfter=after;
    setMeta(3,ThemeTokens.ICON_THUMB,
      after?"拇指点击复测":"拇指点击专项检测",
      after?"Verify thumb adjustment · 10 taps":"Optional · 10 paced thumb taps",
      "屏幕每 2 秒递增一次编号（共 10 次），每次编号变化时在平时易误触区域用拇指轻点一次。");
    LinearLayout l=layout((after?"再测一次 / Retest\n":"拇指点击检测 / Thumb tap check\n")+"每个编号出现时，用拇指轻点一次，共 10 次。\n\nTap ONCE with your thumb per number (10 taps).");
    button(l,after?"开始复测 / Retest":"开始检测 / Start",()->{if(trialAlive()){collecting=true;applyImmersiveFullscreen();setContentView(new ThumbView(after));}});
    button(l,"恢复退出 / Restore & exit",()->send("cancel",""));
  }
  void thumbPlan(){
    collecting=false;thumbBase=candidate;
    setMeta(3,ThemeTokens.ICON_THUMB,"拇指采集完成","Confirm thumb taps","请确认刚才 10 个编号中每个编号都仅轻点了一次，随后将分析是否需要放宽接触面积上限。");
    LinearLayout l=layout("点击完成\n刚才是否每个编号都只点了一次？\nTaps complete. Did you tap ONCE per number?");
    button(l,"确认分析 / Confirm & analyze",()->{
      if(!trialAlive())return;flow.changedThumb();
      if(thumbBefore.proposeSizeCandidate()&&!candidate.thumbOptimized){candidate=candidate.withThumbOptimization(true);plan="本轮有重复／缺报／短触点证据：候选将接触面积上限从2.0cm放宽到2.4cm；保持触点阈值、起落时序与坐标曲线不变。不是根因结论，需同条件复测。\nDuplicate, missing or short taps justify a candidate area limit of 2.4cm instead of 2.0cm. Thresholds, timing and coordinate curves stay unchanged. Not a root-cause finding; retest under the same conditions.";}
      else if(candidate.thumbOptimized)plan="当前已采用2.4cm上限，不继续盲目放宽。保留现有参数；若仍有问题，不把它声明为已修复。\nAlready using 2.4cm. No further widening. Keep parameters; remaining issues are not declared fixed.";
      else plan="本轮未达到生成面积候选的证据门限，保留2.0cm与当前曲线，不无故修改正常触摸；无需重复采集，继续手势确认。\nInsufficient evidence for an area change. Keep 2.0cm and current curves; no unnecessary retest. Check gestures next.";
      record("thumb-plan.txt",thumbBefore.encode()+plan);if(candidate.thumbOptimized==thumbBase.thumbOptimized){review();}else{thumbAfter=true;send("preview",candidate.encode(device));}
    });
    button(l,"重采 / Recapture",()->thumbIntro(false));
  }
  void thumbReview(){
    collecting=false;flow.thumbRetested=true;
    setMeta(3,ThemeTokens.ICON_VERIFY,"拇指复测完成","Confirm thumb optimization","请在下方测试区再次试用拇指点击和常用手势，确认无误后长按保存。");
    LinearLayout l=layout("复测完成，请确认点击正常。\nRetest complete. Confirm taps work normally.");
    addGesturePad(l);
    holdButton(l,"长按保存 / Hold: save",()->{if(trialAlive()&&flow.confirmThumb())send("save","");});
    button(l,"复测 / Retest",()->thumbIntro(true));
    button(l,"恢复拇指 / Restore thumb",()->{candidate=thumbBase;thumbAfter=true;plan="已恢复本轮拇指测试前参数；重新复测后由你确认，未标记修复成功。\nPre-test thumb parameters restored. Retest and confirm; not marked fixed.";flow.changedThumb();send("preview",candidate.encode(device));});
    button(l,"撤销坐标 / Discard coords",()->send("cancel",""));
  }
  void review(){
    setMeta(3,ThemeTokens.ICON_VERIFY,"坐标校准预览","Verify calibrated coordinates",plan.isEmpty()?"请在下方测试区试用轻点、双击、拖动与双指缩放，确认坐标落点准确后再长按保存。":plan);
    LinearLayout l=layout("请在下方轻点、快速双击、拖动和双指缩放。\n确认位置及操作正常后保存。\nTry taps, double taps, dragging and pinch zoom below.\nSave only if position and gestures are correct.");
    addGesturePad(l);
    holdButton(l,"长按保存 / Hold: save",()->{if(trialAlive())send("save","");});
    button(l,"拇指检测 / Thumb test",()->thumbIntro(false));
    button(l,"恢复坐标 / Restore coords",()->send("cancel",""));
  }

  final class ThumbView extends View {
    final boolean after;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);final ThumbTapCapture capture;float x=-1,y;long tapAt=0;
    ThumbView(boolean a){super(MainActivity.this);after=a;capture=new ThumbTapCapture(SystemClock.uptimeMillis()+3000);}
    final Runnable tick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;if(!trialAlive())return;
      if(capture.finished(SystemClock.uptimeMillis())){collecting=false;if(after){thumbAfterData=capture;record("thumb-after.txt",capture.encode());thumbReview();}else{thumbBefore=capture;record("thumb-before.txt",capture.encode());thumbPlan();}return;}invalidate();handler.postDelayed(this,40);}};
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(tick);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(tick);super.onDetachedFromWindow();}
    protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density;
      drawCanvasBackdrop(c,w,h,p);
      long now=SystemClock.uptimeMillis();int r=capture.round(now);

      String title=(after?"修复后拇指采集":"修复前拇指采集")+" · 已记录 "+capture.total()+" 次";
      String cue=now<capture.start
        ?"准备：请在每个编号出现时轻点一次 · Ready: tap ONCE per number"
        :"在容易出问题的位置用拇指轻点一次 · Tap the problem area once";
      float prog=now<capture.start?0f:Math.min(1f,(now-capture.start)/(float)(ThumbTapCapture.ROUNDS*ThumbTapCapture.PERIOD));
      drawFloatingHud(c,w,h*0.18f,title,cue,prog,ui.accentCyan,p,d);

      // Large center beat indicator
      float cx=w/2f,cy=h*0.52f;
      if(now<capture.start){
        long sec=(capture.start-now+999)/1000;
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(3f*d);p.setColor(ui.accentCyan);
        c.drawCircle(cx,cy,56f*d,p);
        p.setStyle(Paint.Style.FILL);p.setColor(Color.WHITE);p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(36f*d);c.drawText(String.valueOf(sec),cx,cy+13f*d,p);
      }else{
        float beatFrac=((now-capture.start)%ThumbTapCapture.PERIOD)/(float)ThumbTapCapture.PERIOD;
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(3f*d);
        p.setColor(Color.argb((int)((1f-beatFrac)*180),56,189,248));
        c.drawCircle(cx,cy,44f*d+beatFrac*36f*d,p);
        p.setStyle(Paint.Style.FILL);p.setColor(0xFF1A2536);
        c.drawCircle(cx,cy,48f*d,p);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(3f*d);p.setColor(ui.accentCyan);
        c.drawCircle(cx,cy,48f*d,p);
        p.setStyle(Paint.Style.FILL);p.setColor(Color.WHITE);p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(32f*d);c.drawText((r+1)+" / 10",cx,cy+11f*d,p);
      }

      // 10-beat dot bar
      float dotSpan=24f*d;
      float startX=cx-(9*dotSpan)/2f,dotY=h*0.72f;
      for(int k=0;k<10;k++){
        float dx=startX+k*dotSpan;
        p.setStyle(Paint.Style.FILL);
        p.setColor(k<r?ui.targetGreen:k==r&&now>=capture.start?ui.targetAmber:0x44FFFFFF);
        c.drawCircle(dx,dotY,k==r&&now>=capture.start?6f*d:4f*d,p);
      }

      if(x>=0){
        float age=Math.min(1f,(now-tapAt)/600f);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2.5f*d);
        p.setColor(Color.argb((int)((1f-age)*200),250,204,21));
        c.drawCircle(x,y,14f*d+age*24f*d,p);
        p.setStyle(Paint.Style.FILL);p.setColor(ui.targetAmber);
        c.drawCircle(x,y,8f*d,p);
      }
    }
    public boolean onTouchEvent(MotionEvent e){if(!collecting)return true;int a=e.getActionMasked(),i=e.getActionIndex();x=e.getX(i);y=e.getY(i);tapAt=SystemClock.uptimeMillis();
      if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN)capture.down(e.getPointerId(i),e.getEventTime());
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_POINTER_UP)capture.up(e.getPointerId(i),e.getEventTime());
      invalidate();return true;}
  }

  final class Targets extends View {
    final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);final double[][] samples=new double[42][];final CalibrationCapture capture=new CalibrationCapture();
    int index=0;boolean pendingTap=false;String hint="";
    final Runnable animTick=new Runnable(){public void run(){if(!collecting||!isAttachedToWindow())return;invalidate();handler.postDelayed(this,50);}};
    Targets(){super(MainActivity.this);activeTargets=this;}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(animTick);}
    double[] target(){int g=index/14,k=(index%14)/2;return g==0?new double[]{Calibration.POS[k],.5}:g==1?new double[]{.5,Calibration.POS[k]}:new double[]{Calibration.POS[k],.04};}
    protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density;
      drawCanvasBackdrop(c,w,h,p);
      double[] t=target();float x=(float)t[0]*(w-1),y=(float)t[1]*(h-1);
      long now=SystemClock.uptimeMillis();
      int g=index/14;
      String phase=g==0?"水平中线 Horizontal":g==1?"垂直中线 Vertical":"顶部边缘 Top Edge";
      float hudY=(t[1]<0.35)?h*0.74f:h*0.22f;
      String title="全屏坐标校准 · "+(index+1)+" / 42 ("+phase+")";
      String cue=hint.isEmpty()?"轻点黄色靶心，变绿后抬手 · Tap yellow target, lift when green":hint;
      boolean green=pendingTap||capture.green(now);
      drawFloatingHud(c,w,hudY,title,cue,(index+(pendingTap?1f:0f))/42f,green?ui.targetGreen:ui.targetAmber,p,d);

      // Hardware shortcut hint pill underneath HUD
      p.setStyle(Paint.Style.FILL);
      p.setColor(0xB8182232);
      float pillW=Math.min(w-32f*d,410f*d),pillH=30f*d;
      RectF chip=new RectF(w/2f-pillW/2f,hudY+46f*d,w/2f+pillW/2f,hudY+46f*d+pillH);
      c.drawRoundRect(chip,999f,999f,p);
      p.setColor(0xCCCBD5E1);p.setTextAlign(Paint.Align.CENTER);p.setTextSize(12.5f*d);
      c.drawText("音量减：跳过此点 (Vol-)   ·   音量加：重采上一点 (Vol+)",w/2f,chip.centerY()+4.5f*d,p);

      drawPrecisionTarget(c,x,y,green,green?1f:0f,now,p,d);
    }
    void advance(){capture.cancel();if(index>=42){collecting=false;candidate=Calibration.fitPartial(samples,original).withThumbOptimization(original.thumbOptimized);record("coordinate-candidate.profile",candidate.encode(device));thumbAfter=false;setMeta(3,ThemeTokens.ICON_LOADING,"正在生成坐标预览","Applying coordinate preview…","");message("正在调整，请稍候…\nAdjusting…");send("preview",candidate.encode(device));}else{pendingTap=false;invalidate();}}
    void skipRegion(){if(!collecting||pendingTap)return;int k=index/2*2;samples[k]=samples[k+1]=null;index=k+2;skippedRegions++;hint="已跳过该区域 · Skipped region";advance();}
    void previous(){if(!collecting||pendingTap||index==0)return;index--;samples[index]=null;capture.cancel();hint="请重新采集此点 · Redo this point";invalidate();}
    public boolean onTouchEvent(MotionEvent e){if(!collecting||pendingTap)return true;int a=e.getActionMasked(),i=e.getActionIndex();double w=Math.max(1,getWidth()-1),h=Math.max(1,getHeight()-1);
      if(a==MotionEvent.ACTION_CANCEL){capture.cancel();return true;}
      if(a==MotionEvent.ACTION_DOWN)capture.cancel();
      if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN){double[] t=target();if(capture.begin(e.getPointerId(i),e.getEventTime(),e.getX(i)/w,e.getY(i)/h,t[0],t[1]))handler.postDelayed(()->{if(isAttachedToWindow())invalidate();},90);}
      if(a==MotionEvent.ACTION_MOVE){int k=e.findPointerIndex(capture.pointer);if(k>=0){for(int j=0;j<e.getHistorySize();j++)capture.move(capture.pointer,e.getHistoricalX(k,j)/w,e.getHistoricalY(k,j)/h);capture.move(capture.pointer,e.getX(k)/w,e.getY(k)/h);}}
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_POINTER_UP){double[] s=capture.end(e.getPointerId(i),e.getEventTime(),e.getX(i)/w,e.getY(i)/h);if(s!=null){samples[index]=original.inverse(s[0],s[1]);hint="已采集，请抬手 · Captured, lift finger";pendingTap=true;handler.postDelayed(()->{if(collecting&&isAttachedToWindow()){index++;hint="";advance();}},500);}}
      invalidate();return true;}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(animTick);capture.cancel();super.onDetachedFromWindow();}
  }

  // ---- Shared Canvas Drawing Helpers ----

  void drawCanvasBackdrop(Canvas c,float w,float h,Paint p){
    c.drawColor(ui.canvasBg);
    p.setStyle(Paint.Style.STROKE);
    p.setStrokeWidth(1f);
    p.setColor(0x0DFFFFFF);
    float step=ui.dp(48);
    for(float x=step;x<w;x+=step)c.drawLine(x,0,x,h,p);
    for(float y=step;y<h;y+=step)c.drawLine(0,y,w,y,p);
  }

  void drawFloatingHud(Canvas c,float w,float cy,String title,String cue,float progress,int accent,Paint p,float d){
    float boxW=Math.min(w-32f*d,540f*d);
    float boxH=78f*d;
    RectF r=new RectF(w/2f-boxW/2f,cy-boxH/2f,w/2f+boxW/2f,cy+boxH/2f);
    p.setStyle(Paint.Style.FILL);p.setColor(0xE0161E2C);
    c.drawRoundRect(r,20f*d,20f*d,p);
    p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.2f*d);p.setColor(0x28FFFFFF);
    c.drawRoundRect(r,20f*d,20f*d,p);

    p.setStyle(Paint.Style.FILL);
    p.setTextAlign(Paint.Align.CENTER);
    p.setColor(Color.WHITE);
    p.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
    p.setTextSize(16f*d);
    c.drawText(title,w/2f,r.top+26f*d,p);

    p.setTypeface(Typeface.DEFAULT);
    p.setColor(0xD8CBD5E1);
    p.setTextSize(13f*d);
    drawCue(c,cue,w/2f,r.top+47f*d,p);

    // Smooth progress bar along bottom of HUD pill
    float barL=r.left+20f*d,barR=r.right-20f*d,barY=r.bottom-14f*d,barH=4.5f*d;
    p.setColor(0x28FFFFFF);
    c.drawRoundRect(new RectF(barL,barY,barR,barY+barH),999f,999f,p);
    float fillW=(barR-barL)*Math.max(0f,Math.min(1f,progress));
    if(fillW>1f){
      p.setColor(accent);
      c.drawRoundRect(new RectF(barL,barY,barL+fillW,barY+barH),999f,999f,p);
    }
  }

  void drawPrecisionTarget(Canvas c,float x,float y,boolean green,float ringProgress,long now,Paint p,float d){
    int color=green?ui.targetGreen:ui.targetAmber;
    float pulse=(float)(0.5+0.5*Math.sin(now/260.0));

    // Soft outer halo
    p.setStyle(Paint.Style.FILL);
    p.setColor(Color.argb(green?55:(int)(28+pulse*32),Color.red(color),Color.green(color),Color.blue(color)));
    c.drawCircle(x,y,(28f+pulse*7f)*d,p);

    // Outer circle
    p.setStyle(Paint.Style.STROKE);
    p.setStrokeWidth(2.6f*d);
    p.setColor(color);
    c.drawCircle(x,y,22f*d,p);

    // Optional post-capture radial arc
    if(green&&ringProgress>0f&&ringProgress<1f){
      p.setStrokeWidth(4f*d);
      p.setStrokeCap(Paint.Cap.ROUND);
      RectF arc=new RectF(x-28f*d,y-28f*d,x+28f*d,y+28f*d);
      c.drawArc(arc,-90f,360f*ringProgress,false,p);
    }

    // Segmented reticle lines leaving center dot crystal clear
    p.setStrokeWidth(2.2f*d);
    float inner=8f*d,outer=34f*d;
    c.drawLine(x-outer,y,x-inner,y,p);
    c.drawLine(x+inner,y,x+outer,y,p);
    c.drawLine(x,y-outer,x,y-inner,p);
    c.drawLine(x,y+inner,x,y+outer,p);

    // Center pinpoint dot
    p.setStyle(Paint.Style.FILL);
    p.setColor(Color.WHITE);
    c.drawCircle(x,y,3.5f*d,p);
  }

  @Override public boolean onKeyDown(int key,KeyEvent e){if(collecting&&activeTargets!=null&&activeTargets.isAttachedToWindow()){if(key==KeyEvent.KEYCODE_VOLUME_DOWN){if(e.getRepeatCount()==0)activeTargets.skipRegion();return true;}if(key==KeyEvent.KEYCODE_VOLUME_UP){if(e.getRepeatCount()==0)activeTargets.previous();return true;}}return super.onKeyDown(key,e);}
  @Override public void onBackPressed(){finish();}
  final Runnable thisActivityCancel=()->cancelOnLeave();
  @Override public void onPause(){
    super.onPause();
    if(exporting)return;
    if(enteringFullscreen&&!hasActiveSession())return;
    leaving=true;noiseCapturing=false;cancelCountdown();collecting=false;handler.removeCallbacks(noisePoll);
    // Let a pending bridge request finish before requesting rollback; never replace its nonce.
    handler.postDelayed(this::cancelOnLeave,100);if(!isFinishing())finish();
  }
  void cancelOnLeave(){if(waiting){handler.postDelayed(this::cancelOnLeave,300);return;}if(tapPreview||tapLearning){tapPreview=false;tapLearning=false;send("tap-cancel","");}else if(noisePreview||!noiseNonce.isEmpty()){noisePreview=false;noiseNonce="";send("noise-cancel","");}else if(preview){preview=false;send("cancel","");}}

  // =========================================================================
  // Modern Material You Theme Tokens & Custom Views
  // =========================================================================

  static final class ThemeTokens {
    final boolean dark;
    final float density;
    final int bg,card,cardAlt,primary,onPrimary,primaryContainer,onPrimaryContainer;
    final int textPrimary,textSecondary,divider,ripple;
    final int warnBg,warnFg,okBg,okFg;
    final int canvasBg=0xFF0E131B,targetAmber=0xFFFACC15,targetGreen=0xFF34D399,accentCyan=0xFF38BDF8,dangerRed=0xFFF87171;

    static final int ICON_IDLE=1,ICON_SWIPE=2,ICON_TAP=3,ICON_VERIFY=4,ICON_FIX=5,ICON_WARN=6,ICON_THUMB=7,ICON_FULLSCREEN=8,ICON_LOADING=9;

    ThemeTokens(Context ctx){
      int night=ctx.getResources().getConfiguration().uiMode&Configuration.UI_MODE_NIGHT_MASK;
      this.dark=night==Configuration.UI_MODE_NIGHT_YES;
      this.density=ctx.getResources().getDisplayMetrics().density;
      if(Build.VERSION.SDK_INT>=31){
        if(dark){
          bg=ctx.getColor(android.R.color.system_neutral1_900);
          card=ctx.getColor(android.R.color.system_neutral1_800);
          cardAlt=ctx.getColor(android.R.color.system_neutral2_800);
          primary=ctx.getColor(android.R.color.system_accent1_200);
          onPrimary=ctx.getColor(android.R.color.system_accent1_900);
          primaryContainer=ctx.getColor(android.R.color.system_accent1_700);
          onPrimaryContainer=ctx.getColor(android.R.color.system_accent1_100);
          textPrimary=ctx.getColor(android.R.color.system_neutral1_50);
          textSecondary=ctx.getColor(android.R.color.system_neutral2_200);
          divider=Color.argb(38,255,255,255);
          ripple=Color.argb(40,255,255,255);
        }else{
          bg=ctx.getColor(android.R.color.system_neutral1_50);
          card=ctx.getColor(android.R.color.system_neutral1_0);
          cardAlt=ctx.getColor(android.R.color.system_neutral2_50);
          primary=ctx.getColor(android.R.color.system_accent1_600);
          onPrimary=Color.WHITE;
          primaryContainer=ctx.getColor(android.R.color.system_accent1_100);
          onPrimaryContainer=ctx.getColor(android.R.color.system_accent1_900);
          textPrimary=ctx.getColor(android.R.color.system_neutral1_900);
          textSecondary=ctx.getColor(android.R.color.system_neutral2_600);
          divider=Color.argb(26,0,0,0);
          ripple=Color.argb(28,0,0,0);
        }
      }else{
        if(dark){
          bg=0xFF111318;card=0xFF1D2026;cardAlt=0xFF262A32;
          primary=0xFFA8C7FA;onPrimary=0xFF062E6F;primaryContainer=0xFF0842A0;onPrimaryContainer=0xFFD3E3FD;
          textPrimary=0xFFE3E5EA;textSecondary=0xFFA8B0BC;divider=0x26FFFFFF;ripple=0x28FFFFFF;
        }else{
          bg=0xFFF2F4F8;card=0xFFFFFFFF;cardAlt=0xFFE9EEF6;
          primary=0xFF1B6EF3;onPrimary=0xFFFFFFFF;primaryContainer=0xFFD3E3FD;onPrimaryContainer=0xFF041E49;
          textPrimary=0xFF1A1C1E;textSecondary=0xFF5E636B;divider=0x1A000000;ripple=0x1C000000;
        }
      }
      if(dark){
        warnBg=0xFF45241E;warnFg=0xFFFFB4A9;okBg=0xFF183829;okFg=0xFF8CE0B0;
      }else{
        warnBg=0xFFFCE8E6;warnFg=0xFFA50E0E;okBg=0xFFE3F5EC;okFg=0xFF137333;
      }
    }

    int dp(float v){return Math.round(v*density);}

    Drawable roundedRect(int fill,float radiusDp){
      GradientDrawable d=new GradientDrawable();
      d.setColor(fill);d.setCornerRadius(dp(radiusDp));return d;
    }

    Drawable roundedRectStroke(int fill,int stroke,float strokeDp,float radiusDp){
      GradientDrawable d=new GradientDrawable();
      d.setColor(fill);d.setCornerRadius(dp(radiusDp));
      d.setStroke(Math.max(1,dp(strokeDp)),stroke);return d;
    }

    Drawable pillRipple(int fill,int stroke){
      GradientDrawable c=new GradientDrawable();
      c.setColor(fill);c.setCornerRadius(dp(999));
      if(stroke!=0)c.setStroke(Math.max(1,dp(1)),stroke);
      GradientDrawable m=new GradientDrawable();
      m.setColor(Color.WHITE);m.setCornerRadius(dp(999));
      return new RippleDrawable(ColorStateList.valueOf(ripple),c,m);
    }

    View iconView(Context ctx,int kind,int color,int sizeDp){
      IconView v=new IconView(ctx,kind,color,density);
      v.setMinimumWidth(dp(sizeDp));v.setMinimumHeight(dp(sizeDp));return v;
    }
  }

  static final class IconView extends View {
    final int kind;final float d;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    IconView(Context ctx,int kind,int color,float density){
      super(ctx);this.kind=kind;this.d=density;
      p.setColor(color);p.setStyle(Paint.Style.STROKE);
      p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeJoin(Paint.Join.ROUND);
    }
    @Override protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),s=Math.min(w,h),cx=w/2f,cy=h/2f;
      p.setStrokeWidth(Math.max(2f,2f*d));
      switch(kind){
        case ThemeTokens.ICON_IDLE:{
          float r=s*0.36f;
          c.drawRoundRect(new RectF(cx-r,cy-r*0.72f,cx+r,cy+r*0.72f),r*0.24f,r*0.24f,p);
          c.drawLine(cx-r*0.45f,cy,cx+r*0.45f,cy,p);
          break;
        }
        case ThemeTokens.ICON_SWIPE:{
          float r=s*0.34f;
          Path wave=new Path();
          wave.moveTo(cx-r,cy+r*0.15f);
          wave.cubicTo(cx-r*0.4f,cy-r*0.65f,cx+r*0.4f,cy+r*0.85f,cx+r,cy-r*0.15f);
          c.drawPath(wave,p);
          c.drawLine(cx+r*0.65f,cy-r*0.48f,cx+r,cy-r*0.15f,p);
          c.drawLine(cx+r*0.62f,cy+r*0.18f,cx+r,cy-r*0.15f,p);
          break;
        }
        case ThemeTokens.ICON_TAP:{
          float r=s*0.26f;
          c.drawCircle(cx,cy,r,p);
          c.drawLine(cx-r*1.5f,cy,cx-r*0.55f,cy,p);
          c.drawLine(cx+r*0.55f,cy,cx+r*1.5f,cy,p);
          c.drawLine(cx,cy-r*1.5f,cx,cy-r*0.55f,p);
          c.drawLine(cx,cy+r*0.55f,cx,cy+r*1.5f,p);
          break;
        }
        case ThemeTokens.ICON_VERIFY:{
          float r=s*0.34f;
          c.drawCircle(cx,cy,r,p);
          c.drawLine(cx-r*0.42f,cy+r*0.02f,cx-r*0.1f,cy+r*0.34f,p);
          c.drawLine(cx-r*0.1f,cy+r*0.34f,cx+r*0.46f,cy-r*0.28f,p);
          break;
        }
        case ThemeTokens.ICON_FIX:{
          float r=s*0.32f;
          c.drawLine(cx-r,cy-r*0.45f,cx+r,cy-r*0.45f,p);
          c.drawLine(cx-r,cy+r*0.45f,cx+r,cy+r*0.45f,p);
          p.setStyle(Paint.Style.FILL);
          c.drawCircle(cx-r*0.35f,cy-r*0.45f,r*0.24f,p);
          c.drawCircle(cx+r*0.35f,cy+r*0.45f,r*0.24f,p);
          p.setStyle(Paint.Style.STROKE);
          break;
        }
        case ThemeTokens.ICON_WARN:{
          float r=s*0.36f;
          Path tri=new Path();
          tri.moveTo(cx,cy-r*0.92f);tri.lineTo(cx+r,cy+r*0.78f);tri.lineTo(cx-r,cy+r*0.78f);tri.close();
          c.drawPath(tri,p);
          c.drawLine(cx,cy-r*0.28f,cx,cy+r*0.22f,p);
          p.setStyle(Paint.Style.FILL);c.drawCircle(cx,cy+r*0.50f,Math.max(2f,1.6f*d),p);
          p.setStyle(Paint.Style.STROKE);
          break;
        }
        case ThemeTokens.ICON_THUMB:{
          float r=s*0.34f;
          c.drawCircle(cx,cy,r,p);
          c.drawCircle(cx,cy,r*0.55f,p);
          p.setStyle(Paint.Style.FILL);c.drawCircle(cx,cy,r*0.18f,p);
          p.setStyle(Paint.Style.STROKE);
          break;
        }
        case ThemeTokens.ICON_FULLSCREEN:{
          float r=s*0.34f,a=r*0.45f;
          c.drawLine(cx-r,cy-r,cx-r+a,cy-r,p);c.drawLine(cx-r,cy-r,cx-r,cy-r+a,p);
          c.drawLine(cx+r,cy-r,cx+r-a,cy-r,p);c.drawLine(cx+r,cy-r,cx+r,cy-r+a,p);
          c.drawLine(cx-r,cy+r,cx-r+a,cy+r,p);c.drawLine(cx-r,cy+r,cx-r,cy+r-a,p);
          c.drawLine(cx+r,cy+r,cx+r-a,cy+r,p);c.drawLine(cx+r,cy+r,cx+r,cy+r-a,p);
          break;
        }
        case ThemeTokens.ICON_LOADING:
        default:{
          float r=s*0.32f;
          c.drawArc(new RectF(cx-r,cy-r,cx+r,cy+r),-60f,270f,false,p);
          break;
        }
      }
    }
  }

  static final class StepperView extends View {
    static final String[] LABELS={"1 静置检测","2 单指滑动","3 单指点击","4 确认校准"};
    final ThemeTokens ui;final int active;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    StepperView(Context ctx,ThemeTokens ui,int active){super(ctx);this.ui=ui;this.active=active;}
    @Override protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density;
      int n=LABELS.length;
      float padX=36f*d;
      float span=Math.max(1f,w-2f*padX);
      float cy=16f*d,r=11f*d;

      // Connecting lines
      for(int i=0;i<n-1;i++){
        float x1=padX+span*i/(n-1)+r+4f*d;
        float x2=padX+span*(i+1)/(n-1)-r-4f*d;
        if(x2>x1){
          p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2.4f*d);
          p.setColor(i<active?ui.okFg:ui.divider);
          c.drawLine(x1,cy,x2,cy,p);
        }
      }

      // Nodes & labels
      p.setTextAlign(Paint.Align.CENTER);
      for(int i=0;i<n;i++){
        float x=padX+span*i/(n-1);
        boolean done=i<active,curr=i==active;
        p.setStyle(Paint.Style.FILL);
        p.setColor(done?ui.okBg:curr?ui.primary:ui.card);
        c.drawCircle(x,cy,r,p);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.8f*d);
        p.setColor(done?ui.okFg:curr?ui.primary:ui.divider);
        c.drawCircle(x,cy,r,p);

        if(done){
          p.setColor(ui.okFg);p.setStrokeWidth(2f*d);p.setStrokeCap(Paint.Cap.ROUND);
          c.drawLine(x-4f*d,cy,x-1f*d,cy+3.5f*d,p);
          c.drawLine(x-1f*d,cy+3.5f*d,x+4.5f*d,cy-3.5f*d,p);
        }else{
          p.setStyle(Paint.Style.FILL);
          p.setColor(curr?ui.onPrimary:ui.textSecondary);
          p.setTextSize(11f*d);p.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
          c.drawText(String.valueOf(i+1),x,cy+4f*d,p);
        }

        p.setStyle(Paint.Style.FILL);
        p.setTextSize(11.5f*d);
        p.setTypeface(curr?Typeface.create("sans-serif-medium",Typeface.BOLD):Typeface.DEFAULT);
        p.setColor(curr?ui.textPrimary:done?ui.okFg:ui.textSecondary);
        c.drawText(LABELS[i],x,h-4f*d,p);
      }
    }
  }

  static final class CountdownRingView extends View {
    final ThemeTokens ui;final String unit;
    float fraction=1f;int seconds;boolean warn=false;
    final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    CountdownRingView(Context ctx,ThemeTokens ui,int initialSeconds,String unit){
      super(ctx);this.ui=ui;this.seconds=initialSeconds;this.unit=unit;
    }
    void update(float frac,int sec){fraction=Math.max(0f,Math.min(1f,frac));seconds=Math.max(0,sec);invalidate();}
    void setWarn(boolean w){if(warn!=w){warn=w;invalidate();}}
    @Override protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density,cx=w/2f,cy=h/2f;
      float stroke=8f*d,r=Math.min(cx,cy)-stroke;
      RectF oval=new RectF(cx-r,cy-r,cx+r,cy+r);

      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(stroke);p.setColor(ui.cardAlt);
      c.drawCircle(cx,cy,r,p);

      p.setStrokeCap(Paint.Cap.ROUND);
      p.setColor(warn?ui.warnFg:ui.primary);
      c.drawArc(oval,-90f,360f*fraction,false,p);

      p.setStyle(Paint.Style.FILL);p.setTextAlign(Paint.Align.CENTER);
      p.setColor(ui.textPrimary);p.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
      p.setTextSize(r*0.62f);
      c.drawText(seconds+"s",cx,cy+r*0.14f,p);

      p.setTypeface(Typeface.DEFAULT);p.setColor(ui.textSecondary);p.setTextSize(11.5f*d);
      c.drawText(unit,cx,cy+r*0.48f,p);
    }
  }

  final class HoldPillButton extends FrameLayout {
    static final long HOLD_MS=680;
    final ThemeTokens ui;final Runnable action,onShortTap;
    final TextView label;final Paint fillPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
    float progress=0f;boolean holding=false,fired=false;long downAt=0;
    final Runnable ticker=new Runnable(){
      public void run(){
        if(!holding)return;
        float f=Math.min(1f,(SystemClock.uptimeMillis()-downAt)/(float)HOLD_MS);
        progress=f;invalidate();
        if(f>=1f){
          holding=false;fired=true;progress=0f;invalidate();
          performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
          action.run();
        }else{
          handler.postDelayed(this,16);
        }
      }
    };
    HoldPillButton(Context ctx,ThemeTokens ui,String text,Runnable action,Runnable onShortTap){
      super(ctx);this.ui=ui;this.action=action;this.onShortTap=onShortTap;
      setWillNotDraw(false);
      setBackground(ui.roundedRectStroke(ui.primaryContainer,ui.primary,1.5f,999f));
      setMinimumHeight(ui.dp(54));
      setClickable(true);setFocusable(true);
      label=new TextView(ctx);
      label.setText(text);
      label.setGravity(Gravity.CENTER);
      label.setTextSize(TypedValue.COMPLEX_UNIT_SP,15.5f);
      label.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
      label.setTextColor(ui.onPrimaryContainer);
      label.setPadding(ui.dp(20),ui.dp(12),ui.dp(20),ui.dp(12));
      addView(label,new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.MATCH_PARENT));
      setOnLongClickListener(v->{
        if(!fired){
          holding=false;fired=true;progress=0f;handler.removeCallbacks(ticker);invalidate();
          performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
          action.run();
        }
        return true;
      });
    }
    @Override protected void onDraw(Canvas c){
      super.onDraw(c);
      if(progress>0f){
        float w=getWidth(),h=getHeight(),r=h/2f;
        Path clip=new Path();
        clip.addRoundRect(new RectF(0,0,w,h),r,r,Path.Direction.CW);
        c.save();c.clipPath(clip);
        fillPaint.setColor(ui.primary);
        fillPaint.setAlpha(ui.dark?140:115);
        c.drawRect(0,0,w*progress,h,fillPaint);
        c.restore();
      }
    }
    @Override public boolean onTouchEvent(MotionEvent e){
      if(waiting)return true;
      int a=e.getActionMasked();
      if(a==MotionEvent.ACTION_DOWN){
        holding=true;fired=false;downAt=SystemClock.uptimeMillis();progress=0.04f;
        handler.removeCallbacks(ticker);handler.post(ticker);invalidate();
        return true;
      }
      if(a==MotionEvent.ACTION_MOVE&&holding){
        if(e.getX()<0||e.getX()>getWidth()||e.getY()<0||e.getY()>getHeight()){
          holding=false;progress=0f;handler.removeCallbacks(ticker);invalidate();
        }
        return true;
      }
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL){
        boolean wasHolding=holding;
        holding=false;progress=0f;handler.removeCallbacks(ticker);invalidate();
        if(a==MotionEvent.ACTION_UP&&wasHolding&&!fired&&onShortTap!=null)onShortTap.run();
        return true;
      }
      return super.onTouchEvent(e);
    }
    @Override protected void onDetachedFromWindow(){
      holding=false;handler.removeCallbacks(ticker);super.onDetachedFromWindow();
    }
  }

  final class SwipeMonitorView extends View {
    final ThemeTokens ui;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    final ArrayList<float[]> trail=new ArrayList<>();
    float fraction=1f;int seconds=20;boolean multiWarn=false;int moveCount=0;boolean topTouched=false;
    float[] activeXY=new float[0];
    SwipeMonitorView(Context ctx,ThemeTokens ui){super(ctx);this.ui=ui;}
    void updateState(float frac,int sec,boolean multi,int moves){
      fraction=Math.max(0f,Math.min(1f,frac));seconds=Math.max(0,sec);multiWarn=multi;moveCount=moves;invalidate();
    }
    void onMotion(MotionEvent e){
      int a=e.getActionMasked(),n=e.getPointerCount();
      if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL){
        activeXY=new float[0];
      }else{
        activeXY=new float[n*2];
        for(int i=0;i<n;i++){
          float px=e.getX(i),py=e.getY(i);
          activeXY[i*2]=px;activeXY[i*2+1]=py;
          if(py<getHeight()*0.22f)topTouched=true;
        }
        if(n>0){
          trail.add(new float[]{e.getX(0),e.getY(0)});
          if(trail.size()>48)trail.remove(0);
        }
      }
      invalidate();
    }
    @Override protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density;
      drawCanvasBackdrop(c,w,h,p);
      float topZoneH=h*0.22f;

      // Top avoid zone
      p.setStyle(Paint.Style.FILL);
      p.setColor(topTouched?0x38F87171:0x1CF87171);
      c.drawRect(0,0,w,topZoneH,p);
      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.5f*d);p.setColor(0x66F87171);
      c.drawLine(0,topZoneH,w,topZoneH,p);

      p.setStyle(Paint.Style.FILL);p.setTextAlign(Paint.Align.CENTER);
      p.setColor(0xFFFCA5A5);p.setTextSize(14f*d);p.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
      c.drawText("请勿触碰顶部区域 · Avoid touching the top edge",w/2f,topZoneH-14f*d,p);

      // Subtle lower swipe wave guide
      float guideY=topZoneH+(h-topZoneH)*0.58f;
      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2.5f*d);
      p.setPathEffect(new DashPathEffect(new float[]{10f*d,10f*d},0));
      p.setColor(0x2838BDF8);
      Path wave=new Path();
      wave.moveTo(w*0.18f,guideY);
      wave.cubicTo(w*0.36f,guideY-54f*d,w*0.64f,guideY+54f*d,w*0.82f,guideY);
      c.drawPath(wave,p);
      p.setPathEffect(null);

      // Glowing finger trail
      if(trail.size()>1){
        p.setStyle(Paint.Style.STROKE);p.setStrokeCap(Paint.Cap.ROUND);
        for(int i=1;i<trail.size();i++){
          float alpha=i/(float)trail.size();
          p.setStrokeWidth((3f+alpha*6f)*d);
          p.setColor(Color.argb((int)(alpha*210),multiWarn?250:56,multiWarn?180:189,multiWarn?60:248));
          c.drawLine(trail.get(i-1)[0],trail.get(i-1)[1],trail.get(i)[0],trail.get(i)[1],p);
        }
      }

      // Active pointers
      for(int i=0;i<activeXY.length;i+=2){
        float px=activeXY[i],py=activeXY[i+1];
        p.setStyle(Paint.Style.FILL);p.setColor(0x3838BDF8);c.drawCircle(px,py,28f*d,p);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2.5f*d);p.setColor(ui.accentCyan);c.drawCircle(px,py,16f*d,p);
      }

      // Floating HUD card in upper-middle safe area
      String title="单指滑动幽灵检测 · 剩余 "+seconds+" 秒";
      String cue=multiWarn?"已检测到伴随触点，请继续单指滑动 · Extra touches detected; keep swiping"
        :"请只用一根手指在下方区域来回滑动 · Swipe with ONE finger below";
      drawFloatingHud(c,w,topZoneH+58f*d,title,cue,1f-fraction,multiWarn?ui.targetAmber:ui.accentCyan,p,d);
    }
  }

  static final class GesturePadView extends View {
    final ThemeTokens ui;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    float[] xy=new float[0];int[] ids=new int[0];
    final ArrayList<float[]> trail=new ArrayList<>();
    GesturePadView(Context ctx,ThemeTokens ui){super(ctx);this.ui=ui;}
    @Override protected void onDraw(Canvas c){
      float w=getWidth(),h=getHeight(),d=ui.density,r=20f*d;
      Path clip=new Path();
      clip.addRoundRect(new RectF(0,0,w,h),r,r,Path.Direction.CW);
      c.save();c.clipPath(clip);

      c.drawColor(ui.canvasBg);

      // Subtle dot grid
      p.setStyle(Paint.Style.FILL);p.setColor(0x18FFFFFF);
      float step=24f*d;
      for(float x=step;x<w;x+=step)for(float y=step;y<h;y+=step)c.drawCircle(x,y,1.3f*d,p);

      // Trail
      if(trail.size()>1){
        p.setStyle(Paint.Style.STROKE);p.setStrokeCap(Paint.Cap.ROUND);
        for(int i=1;i<trail.size();i++){
          float a=i/(float)trail.size();
          p.setStrokeWidth((2f+a*4f)*d);
          p.setColor(Color.argb((int)(a*170),56,189,248));
          c.drawLine(trail.get(i-1)[0],trail.get(i-1)[1],trail.get(i)[0],trail.get(i)[1],p);
        }
      }

      // Active fingers
      for(int i=0;i<xy.length;i+=2){
        float px=xy[i],py=xy[i+1];
        p.setStyle(Paint.Style.FILL);p.setColor(0x3838BDF8);c.drawCircle(px,py,24f*d,p);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2.4f*d);p.setColor(ui.targetAmber);c.drawCircle(px,py,14f*d,p);
        p.setStyle(Paint.Style.FILL);p.setColor(Color.WHITE);c.drawCircle(px,py,4f*d,p);
      }

      // Corner HUD pills
      int active=xy.length/2;
      p.setStyle(Paint.Style.FILL);p.setColor(0xB0182232);
      c.drawRoundRect(new RectF(12f*d,12f*d,126f*d,38f*d),999f,999f,p);
      p.setColor(active>0?ui.targetGreen:0xCCCBD5E1);p.setTextAlign(Paint.Align.LEFT);p.setTextSize(12f*d);
      p.setTypeface(Typeface.create("sans-serif-medium",Typeface.BOLD));
      c.drawText("当前触点: "+active,24f*d,29f*d,p);

      p.setTextAlign(Paint.Align.RIGHT);p.setTypeface(Typeface.DEFAULT);p.setColor(0x99CBD5E1);
      c.drawText("交互测试区 · 轻点 / 双击 / 拖动 / 缩放",w-14f*d,29f*d,p);

      if(active==0&&trail.isEmpty()){
        p.setTextAlign(Paint.Align.CENTER);p.setColor(0x55FFFFFF);p.setTextSize(14f*d);
        c.drawText("在此区域自由触摸以验证手感 · Touch here to verify gestures",w/2f,h/2f+10f*d,p);
      }

      c.restore();
      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.2f*d);p.setColor(ui.divider);
      c.drawRoundRect(new RectF(1,1,w-1,h-1),r,r,p);
    }
    @Override public boolean onTouchEvent(MotionEvent e){
      getParent().requestDisallowInterceptTouchEvent(true);
      int a=e.getActionMasked(),skip=a==MotionEvent.ACTION_POINTER_UP?e.getActionIndex():-1;
      int n=a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL?0:e.getPointerCount()-(skip>=0?1:0);
      xy=new float[n*2];ids=new int[n];
      for(int i=0,j=0;j<n;i++){
        if(i==skip)continue;
        xy[j*2]=e.getX(i);xy[j*2+1]=e.getY(i);ids[j]=e.getPointerId(i);j++;
      }
      if(n==1){
        trail.add(new float[]{xy[0],xy[1]});
        if(trail.size()>36)trail.remove(0);
      }else if(n==0){
        if(trail.size()>12)trail.subList(0,trail.size()-12).clear();
      }
      invalidate();return true;
    }
  }
}
