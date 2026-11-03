package org.surface.touchcalibration;
import java.util.*;
/** Wait for one complete touch, retain duplicate evidence for 1 s, then advance. */
public final class SingleTapCapture {
  public static final int N=15;public static final long AFTER_CAPTURE_MS=1000;
  public final double[][] targets=new double[N][2];public final int[] starts=new int[N],peak=new int[N],groups=new int[N],cancels=new int[N];
  public final long[] shownAt=new long[N],capturedAt=new long[N],endedAt=new long[N];
  public final long start;public boolean complete=false;int active=0,index=0;boolean began=false;
  public SingleTapCapture(long now,long seed){start=now;List<double[]> p=new ArrayList<>();
    Arrays.fill(shownAt,-1);Arrays.fill(capturedAt,-1);Arrays.fill(endedAt,-1);shownAt[0]=now;
    for(double y:new double[]{.12,.5,.88})for(double x:new double[]{.08,.29,.5,.71,.92})p.add(new double[]{x,y});
    Collections.shuffle(p,new Random(seed));for(int i=0;i<N;i++)targets[i]=p.get(i);
  }
  public int trial(long now){return now<start?-1:index;}
  public boolean finished(long now){return index>=N;}
  public int completed(){return index;}
  public boolean captured(){return index<N&&capturedAt[index]>=0;}
  public long remaining(long now){return captured()?Math.max(0,AFTER_CAPTURE_MS-(now-capturedAt[index])):AFTER_CAPTURE_MS;}
  /** Only the UI tick advances: no event can be assigned to an unseen target. */
  public void tick(long now){
    if(now<start||index>=N||!captured()||remaining(now)>0||active>0)return;
    endedAt[index]=now;index++;began=false;
    if(index<N)shownAt[index]=now;else complete=true;
  }
  public void event(int action,int pointerCount,long now){
    int i=trial(now);if(i<0||i>=N)return;
    if(action==0){starts[i]++;groups[i]++;began=true;}else if(action==5)starts[i]++;
    if(action==3){cancels[i]++;began=false;}
    if(action==0||action==2||action==5)peak[i]=Math.max(peak[i],pointerCount);
    active=action==1||action==3?0:Math.max(0,pointerCount-(action==6?1:0));
    if(action==1&&began){if(capturedAt[i]<0)capturedAt[i]=now;began=false;}
  }
  public int total(){int n=0;for(int v:starts)n+=v;return n;}
  public int multiTrials(){int n=0;for(int v:peak)if(v>1)n++;return n;}
  public int fragmentedTrials(){int n=0;for(int v:groups)if(v>1)n++;return n;}
  public int missingTrials(){int n=0;for(int v:starts)if(v==0)n++;return n;}
}
