package org.surface.touchcalibration;
/** User confirmation is distinct from measurements; never inferred from a low count. */
public final class WizardFlow {
  public boolean retested=false,noiseConfirmed=false,thumbRetested=false,thumbConfirmed=false;
  public boolean tapRetested=false,tapConfirmed=false;
  public void changedTap(){tapRetested=tapConfirmed=false;}
  public boolean confirmTap(){if(!tapRetested)return false;tapConfirmed=true;return true;}
  public void useOriginalTap(){tapConfirmed=true;}
  public void changedNoise(){retested=noiseConfirmed=false;}
  public void finishRetest(){retested=true;noiseConfirmed=false;}
  public boolean confirmNoise(){if(!retested)return false;noiseConfirmed=true;return true;}
  public void changedThumb(){thumbRetested=thumbConfirmed=false;}
  public boolean confirmThumb(){if(!thumbRetested)return false;thumbConfirmed=true;return true;}
}
