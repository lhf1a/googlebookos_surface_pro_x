package org.surface.touchcalibration;
import java.util.*;
/** Ten externally paced intended taps, NOT ten events (fragments must remain visible). */
public final class ThumbTapCapture {
  public static final int ROUNDS=10;public static final long PERIOD=2000;
  public final int[] contacts=new int[ROUNDS],shortContacts=new int[ROUNDS];
  long start;final Map<Integer,Long> downs=new HashMap<>();
  public ThumbTapCapture(long time){start=time;}
  public int round(long time){return (int)Math.floorDiv(time-start,PERIOD);}
  public boolean finished(long time){return time-start>=ROUNDS*PERIOD;}
  public void down(int id,long time){int r=round(time);if(r<0||r>=ROUNDS||downs.containsKey(id))return;contacts[r]++;downs.put(id,time);}
  public void up(int id,long time){Long at=downs.remove(id);if(at==null)return;int r=round(at);if(time-at<60&&r>=0&&r<ROUNDS)shortContacts[r]++;}
  public int total(){int n=0;for(int v:contacts)n+=v;return n;}
  public int extras(){int n=0;for(int v:contacts)n+=Math.max(0,v-1);return n;}
  public int missing(){int n=0;for(int v:contacts)if(v==0)n++;return n;}
  public int shorts(){int n=0;for(int v:shortContacts)n+=v;return n;}
  public boolean proposeSizeCandidate(){return extras()>=2||missing()>=2||shorts()>=3;}
  public String summary(){return "10次提示 / 上报触点 "+total()+" / 同轮额外 "+extras()+" / 无上报轮 "+missing()+" / <60ms短触点 "+shorts();}
  public String encode(){return "contacts="+Arrays.toString(contacts)+"\nshort_contacts="+Arrays.toString(shortContacts)+"\n";}
}
