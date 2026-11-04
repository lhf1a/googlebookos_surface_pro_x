package org.surface.touchcalibration;
import java.util.*;
/** Screen-normalized evidence, no ROI exclusions, no coordinate fitting or timing filters. */
public final class SlideCapture {
  static final class Track {double x,y,path,minX,maxX,minY,maxY;int moves;Track(double a,double b){x=minX=maxX=a;y=minY=maxY=b;}}
  final Map<Integer,Track> tracks=new HashMap<>();
  public int peak=0,multiFrames=0,cancels=0;public double path=0,span=0;public int moves=0;
  public long first=-1,last=-1;
  public void frame(long now,int[] ids,double[] xs,double[] ys,boolean cancel){
    if(cancel){cancels++;tracks.clear();return;}
    if(ids.length==0){tracks.clear();return;}
    if(first<0)first=now;last=now;peak=Math.max(peak,ids.length);if(ids.length>1)multiFrames++;
    Set<Integer> alive=new HashSet<>();
    for(int i=0;i<ids.length;i++){
      double x=xs[i],y=ys[i];if(!Double.isFinite(x)||!Double.isFinite(y))continue;
      alive.add(ids[i]);Track t=tracks.get(ids[i]);if(t==null){tracks.put(ids[i],new Track(x,y));continue;}
      double d=Math.hypot(x-t.x,y-t.y);if(d>.001&&d<.2){t.path+=d;t.moves++;}
      t.x=x;t.y=y;t.minX=Math.min(t.minX,x);t.maxX=Math.max(t.maxX,x);t.minY=Math.min(t.minY,y);t.maxY=Math.max(t.maxY,y);
      path=Math.max(path,t.path);moves=Math.max(moves,t.moves);span=Math.max(span,Math.hypot(t.maxX-t.minX,t.maxY-t.minY));
    }
    tracks.keySet().retainAll(alive);
  }
  // Real motion is required, not arbitrary event-count, distance, span or duration quotas.
  public boolean enough(){return first>=0&&last>first&&moves>0&&cancels==0;}
  public boolean clean(){return enough()&&peak==1&&multiFrames==0;}
  public String summary(){return "enough="+enough()+" peak="+peak+" multi_frames="+multiFrames+" moves="+moves+" path="+path+" span="+span+" cancels="+cancels;}
}
