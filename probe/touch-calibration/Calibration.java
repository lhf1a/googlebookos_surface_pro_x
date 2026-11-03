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
        if(!Double.isFinite(x[i])||!Double.isFinite(y[i])||x[i]<0||x[i]>1||y[i]<0||y[i]>1)throw new IllegalArgumentException("曲线包含非有限或越界值");
        if(i>0){double slope=(y[i]-y[i-1])/(x[i]-x[i-1]);if(x[i]<=x[i-1]||!Double.isFinite(slope)||slope<=0)throw new IllegalArgumentException("测量点无法可靠区分，请重新校准");}
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
    // Repeated taps supply weighted evidence. Disagreement lowers weight, never
    // invalidates the entire session. Pool-adjacent-violators prevents foldovers.
    List<double[]> points=new ArrayList<>();
    for(int i=0;i<POS.length;i++){
      double a=measured[i*2][axis],b=measured[i*2+1][axis];
      if(!Double.isFinite(a)||!Double.isFinite(b))continue;
      double raw=Math.max(0,Math.min(1,(a+b)/2));
      if(raw<=.00001||raw>=.99999)continue; // Saturation has no invertible information.
      double weight=1/Math.pow(1+Math.abs(a-b)*25,2);
      points.add(new double[]{raw,POS[i],weight});
    }
    points.sort((a,b)->Double.compare(a[0],b[0]));
    List<double[]> unique=new ArrayList<>();
    for(double[] p:points){
      if(!unique.isEmpty()&&p[0]-unique.get(unique.size()-1)[0]<.00002){
        double[] q=unique.get(unique.size()-1);double w=p[2]+q[2];
        q[0]=(q[0]*q[2]+p[0]*p[2])/w;q[1]=(q[1]*q[2]+p[1]*p[2])/w;q[2]=w;
      }else unique.add(p.clone());
    }
    int n=unique.size();if(n==0)return new Curve(new double[]{0,1},new double[]{0,1});
    List<double[]> blocks=new ArrayList<>();
    for(int i=0;i<n;i++){
      double[] p=unique.get(i);blocks.add(new double[]{i,i,p[1]*p[2],p[2]});
      while(blocks.size()>1){
        int k=blocks.size();double[] p0=blocks.get(k-2),p1=blocks.get(k-1);
        if(p0[2]/p0[3]<p1[2]/p1[3])break;
        p0[1]=p1[1];p0[2]+=p1[2];p0[3]+=p1[3];blocks.remove(k-1);
      }
    }
    double[] x=new double[n+2],y=new double[n+2];x[n+1]=y[n+1]=1;
    for(int i=0;i<n;i++)x[i+1]=unique.get(i)[0];
    for(double[] block:blocks)for(int i=(int)block[0];i<=(int)block[1];i++)y[i+1]=block[2]/block[3];
    // Tiny regularisation keeps serialization strictly monotone, not a cap on offset.
    for(int i=1;i<=n;i++)y[i]=Math.max(y[i-1]+.00001,Math.min(1-(n+1-i)*.00001,y[i]));
    return new Curve(x,y);
  }
  public static Calibration fit(double[][] samples){
    if(samples.length!=42)throw new IllegalArgumentException();
    return new Calibration(fitAxis(Arrays.copyOfRange(samples,0,14),0),fitAxis(Arrays.copyOfRange(samples,14,28),1),fitAxis(Arrays.copyOfRange(samples,28,42),0));
  }
  /** Missing regions retain prior inverse anchors, not invented touch samples. */
  public static Calibration fitPartial(double[][] samples,Calibration prior){
    if(samples.length!=42)throw new IllegalArgumentException();
    Curve[] curves=new Curve[3];
    for(int group=0;group<3;group++){
      double[][] axis=new double[14][];int measured=0;
      for(int point=0;point<7;point++){
        int i=group*14+point*2;
        if(samples[i]!=null&&samples[i+1]!=null){axis[point*2]=samples[i];axis[point*2+1]=samples[i+1];measured++;}
        else{
          double[] anchor=prior.inverse(group==1?.5:POS[point],group==1?POS[point]:group==0?.5:.04);
          axis[point*2]=anchor;axis[point*2+1]=anchor;
        }
      }
      curves[group]=measured<2?(group==0?prior.x:group==1?prior.y:prior.top):fitAxis(axis,group==1?1:0);
    }
    return new Calibration(curves[0],curves[1],curves[2],prior.thumbOptimized);
  }
  public String encode(String device){return "Version=1\nDevice="+device+"\nCalibrationX="+x.encode()+"\nCalibrationY="+y.encode()+"\nCalibrationXTop="+top.encode()+"\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\nContactSizeMax="+(thumbOptimized?"2.4":"2.0")+"\n";}
}
