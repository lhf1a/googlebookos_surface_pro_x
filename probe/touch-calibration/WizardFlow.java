package org.surface.touchcalibration;
/** User confirmation is distinct from measurements; never inferred from a low count. */
public final class WizardFlow {
  /** Residual signal is separate from observed contacts; reject incomplete captures. */
  public static boolean idleOutputClean(String state,int frames,int contacts){
    return frames>0&&contacts==0&&(state.equals("quiet")||state.equals("ready")||state.equals("line_noise")||state.equals("broad_signal")||state.equals("transient")||state.equals("unresolved"));
  }
  public boolean retested=false,noiseConfirmed=false,thumbRetested=false,thumbConfirmed=false;
  public boolean slideRetested=false,slideConfirmed=false;
  public void changedSlide(){slideRetested=slideConfirmed=false;}
  public boolean confirmSlide(){if(!slideRetested)return false;slideConfirmed=true;return true;}
  public boolean tapRetested=false,tapConfirmed=false;
  public void changedTap(){tapRetested=tapConfirmed=false;}
  public boolean confirmTap(){if(!tapRetested)return false;tapConfirmed=true;return true;}
  public void useOriginalTap(){tapConfirmed=true;}
  public void changedNoise(){retested=noiseConfirmed=false;changedSlide();}
  public void finishRetest(){retested=true;noiseConfirmed=false;}
  public boolean confirmNoise(){if(!retested)return false;noiseConfirmed=true;return true;}
  public void changedThumb(){thumbRetested=thumbConfirmed=false;}
  public boolean confirmThumb(){if(!thumbRetested)return false;thumbConfirmed=true;return true;}
}
