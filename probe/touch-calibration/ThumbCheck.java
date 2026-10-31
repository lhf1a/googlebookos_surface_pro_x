package org.surface.touchcalibration;
/** Pure gesture state: early release is a test observation, not a diagnosis. */
public final class ThumbCheck {
  public int completed=0;
  public boolean down=false;
  long began;double startX,startY,maxDistance;boolean stayedEdge;
  static boolean edge(double x,double y){return x<.08||x>.92||y<.08||y>.92;}
  public boolean begin(long time,double x,double y){
    if(!edge(x,y))return false;
    down=true;began=time;startX=x;startY=y;maxDistance=0;stayedEdge=true;return true;
  }
  public void move(double x,double y){if(down){maxDistance=Math.max(maxDistance,Math.hypot(x-startX,y-startY));stayedEdge&=edge(x,y);}}
  public long remaining(long now){return down?Math.max(0,5000-(now-began)):5000;}
  public String end(long now,double x,double y){
    if(!down)return "ignore";move(x,y);down=false;
    if(now-began<5000)return "early";
    if(!stayedEdge||maxDistance<.04)return "retry";
    completed++;return completed>=3?"complete":"next";
  }
  public void cancel(){down=false;}
}
