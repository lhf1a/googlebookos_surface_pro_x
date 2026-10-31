package org.surface.touchcalibration;
import java.util.*;

/** Matches the output-only IPTSd curves; no sensor access or detection changes. */
public final class Calibration {
  public static final double[] POS={.015,.04,.08,.5,.92,.96,.985};
  public static final class Curve {
    final double[] x,y;
    Curve(double[] a,double[] b){x=a;y=b;validate();}
    public Curve(String text){
      if(text.trim().isEmpty()){x=new double[]{0,1};y=x;return;}
      String[] pairs=text.trim().split("\\s+");x=new double[pairs.length];y=new double[pairs.length];
      for(int i=0;i<pairs.length;i++){String[] p=pairs[i].split(":");if(p.length!=2)throw new IllegalArgumentException();x[i]=Double.parseDouble(p[0]);y[i]=Double.parseDouble(p[1]);}
      validate();
    }
    void validate(){
      int n=x.length;if(n<2||n>9||x[0]!=0||y[0]!=0||x[n-1]!=1||y[n-1]!=1)throw new IllegalArgumentException("曲线端点无效");
      for(int i=0;i<n;i++){
        if(!Double.isFinite(x[i])||!Double.isFinite(y[i])||x[i]<0||x[i]>1||y[i]<0||y[i]>1||Math.abs(x[i]-y[i])>.08)throw new IllegalArgumentException("测量偏差过大，请重新落指");
        if(i>0){double slope=(y[i]-y[i-1])/(x[i]-x[i-1]);if(x[i]<=x[i-1]||slope<.2||slope>6)throw new IllegalArgumentException("测量点无法可靠区分，请重新校准");}
      }
    }
    public double map(double v){if(v<=0)return 0;if(v>=1)return 1;int i=1;while(x[i]<v)i++;return y[i-1]+(v-x[i-1])*(y[i]-y[i-1])/(x[i]-x[i-1]);}
    String encode(){StringBuilder s=new StringBuilder();for(int i=0;i<x.length;i++){if(i>0)s.append(' ');s.append(String.format(Locale.ROOT,"%.6f:%.6f",x[i],y[i]));}return s.toString();}
  }
  public final Curve x,y,top;
  public final boolean thumbOptimized;
  public Calibration(String body){
    Map<String,String> m=new HashMap<>();for(String line:body.split("\n")){int p=line.indexOf('=');if(p>0)m.put(line.substring(0,p).trim(),line.substring(p+1).trim());}
    x=new Curve(m.getOrDefault("CalibrationX",""));y=new Curve(m.getOrDefault("CalibrationY",""));String t=m.getOrDefault("CalibrationXTop","");top=t.trim().isEmpty()?x:new Curve(t);
    String size=m.getOrDefault("ContactSizeMax","2.0");if(!size.equals("2.0")&&!size.equals("2.4"))throw new IllegalArgumentException("未知拇指选项");thumbOptimized=size.equals("2.4");
  }
  Calibration(Curve a,Curve b,Curve c){this(a,b,c,false);}
  Calibration(Curve a,Curve b,Curve c,boolean thumb){x=a;y=b;top=c;thumbOptimized=thumb;}
  public Calibration withThumbOptimization(boolean enabled){return new Calibration(x,y,top,enabled);}
  public double[] map(double a,double b){double w=Math.max(0,Math.min(1,(b-.1)/.2));return new double[]{top.map(a)*(1-w)+x.map(a)*w,y.map(b)};}
  public double[] inverse(double a,double b){
    double lo=0,hi=1;for(int i=0;i<40;i++){double m=(lo+hi)/2;if(y.map(m)<b)lo=m;else hi=m;}double rawY=(lo+hi)/2;
    lo=0;hi=1;for(int i=0;i<40;i++){double m=(lo+hi)/2;if(map(m,rawY)[0]<a)lo=m;else hi=m;}return new double[]{(lo+hi)/2,rawY};
  }
  static Curve fitAxis(double[][] measured,int axis){
    // Pool overlapping raw positions instead of inventing distinction at saturated edges.
    List<double[]> pooled=new ArrayList<>();
    for(int i=0;i<POS.length;i++){
      double a=measured[i*2][axis],b=measured[i*2+1][axis];
      if(Math.abs(a-b)>.02)throw new IllegalArgumentException("同一位置两次落点差异过大，请重新校准");
      pooled.add(new double[]{(a+b)/2,POS[i],1});
      while(pooled.size()>1){int n=pooled.size();double[] p=pooled.get(n-2),q=pooled.get(n-1);if(q[0]-p[0]>.0015)break;double w=p[2]+q[2];pooled.remove(n-1);pooled.set(n-2,new double[]{(p[0]*p[2]+q[0]*q[2])/w,(p[1]*p[2]+q[1]*q[2])/w,w});}
    }
    double[] x=new double[pooled.size()+2],y=new double[x.length];x[x.length-1]=y[y.length-1]=1;
    for(int i=0;i<pooled.size();i++){x[i+1]=pooled.get(i)[0];y[i+1]=pooled.get(i)[1];}
    return new Curve(x,y);
  }
  public static Calibration fit(double[][] samples){
    if(samples.length!=42)throw new IllegalArgumentException();
    return new Calibration(fitAxis(Arrays.copyOfRange(samples,0,14),0),fitAxis(Arrays.copyOfRange(samples,14,28),1),fitAxis(Arrays.copyOfRange(samples,28,42),0));
  }
  public String encode(String device){return "Version=1\nDevice="+device+"\nCalibrationX="+x.encode()+"\nCalibrationY="+y.encode()+"\nCalibrationXTop="+top.encode()+"\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\nContactSizeMax="+(thumbOptimized?"2.4":"2.0")+"\n";}
}
