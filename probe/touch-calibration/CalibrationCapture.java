package org.surface.touchcalibration;
import java.util.*;
/** Full-window, target-distance-independent sampling after confirmed noise mitigation. */
public final class CalibrationCapture {
  public int pointer=-1; long began; final List<double[]> points=new ArrayList<>();
  public boolean begin(int id,long time,double x,double y,double tx,double ty){
    if(pointer>=0||!Double.isFinite(x)||!Double.isFinite(y))return false;
    pointer=id;began=time;points.clear();move(id,x,y);return true;
  }
  public void move(int id,double x,double y){
    if(id==pointer&&pointer>=0&&Double.isFinite(x)&&Double.isFinite(y)){
      if(points.size()==256)points.remove(0);points.add(new double[]{x,y});
    }
  }
  public void other(int id,double x,double y){} // Other pointer releases never own this sample.
  public boolean green(long time){return pointer>=0&&time-began>=80;}
  static double median(double[] a){Arrays.sort(a);return (a[(a.length-1)/2]+a[a.length/2])/2;}
  public double[] end(int id,long time,double x,double y){
    if(pointer<0||pointer!=id)return null;
    move(id,x,y);pointer=-1;
    if(points.isEmpty())return null;
    // Robust centre, not an all-or-nothing spatial/last-point validity gate.
    double[] a=new double[points.size()],b=new double[a.length];
    for(int i=0;i<a.length;i++){a[i]=points.get(i)[0];b[i]=points.get(i)[1];}
    return new double[]{median(a),median(b)};
  }
  public void cancel(){pointer=-1;points.clear();}
}
