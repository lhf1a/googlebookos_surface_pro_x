import android.content.Context;
import android.content.AttributionSource;
import android.net.*;
import android.os.*;
import java.io.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.lang.Process;

/** Owns one real IPv4 RMNET bearer and its Android NetworkAgent.
 * shortcut: single private APN/unit, IPv4 data only; a radio HAL is required
 * for subscription UI, SIM switching, SMS and voice.
 */
public final class Sq2Lte {
 static Object call(Object o,String name,Class<?>[] types,Object... values)throws Exception {
  return o.getClass().getMethod(name,types).invoke(o,values);
 }
 static Object set(Object o,String name,Class<?> type,Object value)throws Exception {
  return call(o,name,new Class<?>[]{type},value);
 }
 static Object build(Object o)throws Exception{return call(o,"build",new Class<?>[]{});}
 static void log(String s){System.out.println("SQ2_LTE "+s);}
 static class Agent extends NetworkAgent {
  Agent(Context c,NetworkCapabilities n,LinkProperties p,NetworkAgentConfig a){
   super(c,Looper.getMainLooper(),"SQ2 LTE",n,p,50,a,null);
  }
  public void onNetworkUnwanted(){log("network unwanted");}
  public void onValidationStatus(int s,Uri u){log("validation="+s);}
 }
 final Context context;
 final ConnectivityManager cm;
 final String directory,apn;
 final Binder providerToken=new Binder();
 Object activityManager,provider;
 Process session;
 Thread sessionReader;
 Agent agent;
 ConnectivityManager.NetworkCallback request;
 String address;
 volatile boolean ready,stopping;
 final Map<String,String> settings=new HashMap<>();
 final StringBuilder transcript=new StringBuilder();
 Sq2Lte(Context c,String dir,String apn){context=c;cm=(ConnectivityManager)c.getSystemService(Context.CONNECTIVITY_SERVICE);directory=dir;this.apn=apn;}
 boolean policy(String key,boolean fallback)throws Exception {
  // app_process is not an ActivityManager application; use the same external
  // provider API as Android's settings command, with an owned binder token.
  if(provider==null){
   activityManager=Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
   Object holder=Class.forName("android.app.IActivityManager").getMethod("getContentProviderExternal",String.class,int.class,IBinder.class,String.class)
    .invoke(activityManager,"settings",0,providerToken,"sq2-lte");
   provider=holder.getClass().getField("provider").get(holder);
  }
  Bundle extras=new Bundle();extras.putInt("_user",0);
  Bundle result=(Bundle)Class.forName("android.content.IContentProvider").getMethod("call",AttributionSource.class,String.class,String.class,String.class,Bundle.class)
   .invoke(provider,new AttributionSource.Builder(0).build(),"settings","GET_global",key,extras);
  String value=result==null?null:result.getString("value");
  return value==null?fallback:!value.equals("0");
 }

 static String command(String... args)throws Exception {
  Process p=new ProcessBuilder(args).redirectErrorStream(true).start();
  ByteArrayOutputStream out=new ByteArrayOutputStream();
  Thread reader=new Thread(()->{try{int b;while((b=p.getInputStream().read())!=-1){if(out.size()<16384)out.write(b);}}catch(IOException ignored){}});
  reader.start();
  if(!p.waitFor(20,TimeUnit.SECONDS)){p.destroyForcibly();reader.join(2000);throw new IOException("command timeout: "+args[0]);}
  reader.join(2000);
  String result=out.toString("UTF-8");
  if(p.exitValue()!=0)throw new IOException("command failed: "+args[0]+": "+result);
  return result;
 }
 Map<Integer,String> ports()throws Exception {
  Map<Integer,String> p=new HashMap<>();
  for(String line:command("/vendor/bin/qrtr-lookup").split("\n")){
   String[] f=line.trim().split("\\s+");
   if(f.length>=5&&f[0].matches("[0-9]+")&&f[3].equals("0")&&f[4].matches("[0-9]+")){
    int service=Integer.parseInt(f[0]);
    if(service==1||service==2||service==26||service==47){
     if(p.put(service,f[4])!=null)throw new IOException("ambiguous QMI service "+service);
    }
   }
  }
  for(int id:new int[]{1,2,26,47})if(!p.containsKey(id))throw new IOException("missing QMI service "+id);
  return p;
 }
 void radio(boolean airplane)throws Exception {
  command(directory+"/sq2-qmi-mode","0",ports().get(2),airplane?"low-power":"online");
  log(airplane?"radio low-power":"radio online");
 }
 void start()throws Exception {
  Map<Integer,String> p=ports();ready=false;settings.clear();transcript.setLength(0);
  session=new ProcessBuilder(directory+"/sq2-qmi-data",p.get(47),p.get(26),p.get(1),apn).redirectErrorStream(true).start();
  final Process current=session;
  sessionReader=new Thread(()->{try(BufferedReader r=new BufferedReader(new InputStreamReader(current.getInputStream()))){
   String line;
   while((line=r.readLine())!=null){
    synchronized(settings){
     if(transcript.length()<8192)transcript.append(line).append('\n');
     int equals=line.indexOf('=');
     if(equals>0)settings.put(line.substring(0,equals),line.substring(equals+1));
     if(line.equals("BEARER_READY=1"))ready=true;
    }
   }
  }catch(IOException e){if(current.isAlive())log("QMI reader: "+e);}},"LTE QMI");
  sessionReader.start();
 }
 void publish()throws Exception {
  String ip,dns1,dns2;
  synchronized(settings){ip=settings.get("IP_ADDRESS");dns1=settings.get("DNS1");dns2=settings.get("DNS2");}
  for(String s:new String[]{ip,dns1,dns2})if(!validIpv4(s))throw new IOException("missing/invalid IPv4 configuration");
  command("/system/bin/ip","addr","add",ip+"/32","dev","rmnet_data0");address=ip;
  Object cb=Class.forName("android.net.NetworkCapabilities$Builder").getConstructor().newInstance();
  set(cb,"addTransportType",int.class,0);
  // INTERNET, NOT_RESTRICTED, TRUSTED, NOT_VPN, NOT_CONGESTED,
  // NOT_SUSPENDED, NOT_VCN_MANAGED. No fabricated validation or free-data claim.
  for(int cap:new int[]{12,13,14,15,20,21,28})set(cb,"addCapability",int.class,cap);
  set(cb,"removeCapability",int.class,11);
  LinkProperties lp=new LinkProperties();
  set(lp,"setInterfaceName",String.class,"rmnet_data0");
  set(lp,"addLinkAddress",LinkAddress.class,LinkAddress.class.getConstructor(String.class).newInstance(ip+"/32"));
  set(lp,"setMtu",int.class,1496); // Parent 1500 minus QMAP header; do not raise the tested IPA MTU.
  for(String dns:new String[]{dns1,dns2})set(lp,"addDnsServer",InetAddress.class,InetAddress.getByName(dns));
  set(lp,"addRoute",RouteInfo.class,RouteInfo.class.getConstructor(IpPrefix.class,InetAddress.class,String.class)
   .newInstance(new IpPrefix(InetAddress.getByName("0.0.0.0"),0),null,"rmnet_data0"));
  Object config=Class.forName("android.net.NetworkAgentConfig$Builder").getConstructor().newInstance();
  set(config,"setLegacyType",int.class,0);set(config,"setLegacyTypeName",String.class,"MOBILE");
  set(config,"setLegacySubType",int.class,13);set(config,"setLegacySubTypeName",String.class,"LTE");
  request=new ConnectivityManager.NetworkCallback(){};
  cm.requestNetwork(new NetworkRequest.Builder().addTransportType(0).addCapability(12).build(),request);
  agent=new Agent(context,(NetworkCapabilities)build(cb),lp,(NetworkAgentConfig)build(config));
  Object network=call(agent,"register",new Class<?>[]{});
  call(agent,"markConnected",new Class<?>[]{});
  log("connected network="+network+" metered IPv4");
 }
 static boolean validIpv4(String s){
  if(s==null||!s.matches("[0-9]{1,3}(\\.[0-9]{1,3}){3}")||s.equals("0.0.0.0"))return false;
  for(String b:s.split("\\."))if(Integer.parseInt(b)>255)return false;
  return true;
 }
 synchronized void stop(){
  try{if(agent!=null)call(agent,"unregister",new Class<?>[]{});}catch(Exception e){log("unregister: "+e);}agent=null;
  try{if(request!=null)cm.unregisterNetworkCallback(request);}catch(Exception e){log("release request: "+e);}request=null;
  if(session!=null){session.destroy();try{if(!session.waitFor(35,TimeUnit.SECONDS))session.destroyForcibly();}catch(InterruptedException e){Thread.currentThread().interrupt();}session=null;}
  if(sessionReader!=null){try{sessionReader.join(2000);}catch(InterruptedException e){Thread.currentThread().interrupt();}sessionReader=null;}
  if(address!=null){try{command("/system/bin/ip","addr","del",address+"/32","dev","rmnet_data0");}catch(Exception e){log("remove address: "+e);}address=null;}
  ready=false;
 }
 void run(){
  boolean policyKnown=false,lastAir=false,lastData=false,radioReady=false;
  long nextAttempt=0,started=0;int delay=5;
  try{
   while(!stopping){
    boolean air=policy("airplane_mode_on",true);
    boolean data=policy("mobile_data",false);
    if(!policyKnown||air!=lastAir||data!=lastData){
     stop();if(!policyKnown||air!=lastAir)radioReady=false;
     lastAir=air;lastData=data;policyKnown=true;delay=5;nextAttempt=0;
     log("policy airplane="+air+" data="+data);
    }
    long now=SystemClock.elapsedRealtime();
    try{
     if(!radioReady&&now>=nextAttempt){radio(air);radioReady=true;}
     if(radioReady&&!air&&data&&session==null&&now>=nextAttempt){start();started=now;}
     if(session!=null){
      if(!session.isAlive())throw new IOException("bearer ended");
      if(ready&&agent==null)publish();
      if(!ready&&now-started>90000)throw new IOException("bearer setup timeout");
      if(agent!=null&&now-started>60000)delay=5;
     }
    }catch(Exception e){
     stop();log("connection: "+e);
     synchronized(settings){if(transcript.length()>0){log(transcript.toString());transcript.setLength(0);}}
     nextAttempt=now+delay*1000L;delay=Math.min(300,delay*2);
    }
    Thread.sleep(1000);
   }
  }catch(Exception e){log("service stopped: "+e);}finally{stop();System.exit(1);}
 }
 public static void main(String[] args)throws Exception {
  if(args.length==1&&args[0].equals("selftest")){
   if(!validIpv4("10.5.158.140")||validIpv4("256.1.1.1")||validIpv4("1.2.3")||validIpv4("0.0.0.0")||validIpv4("1.2.3.4;id"))throw new AssertionError();
   System.out.println("PASS IPv4 boundary checks");return;
  }
  if(args.length!=2||!args[0].startsWith("/")||!args[1].matches("[A-Za-z0-9.-]{1,100}"))throw new IllegalArgumentException("directory apn");
  Looper.prepareMainLooper();Class<?> at=Class.forName("android.app.ActivityThread");
  Context c=(Context)at.getMethod("getSystemContext").invoke(at.getMethod("systemMain").invoke(null));
  Sq2Lte service=new Sq2Lte(c,args[0],args[1]);
  Runtime.getRuntime().addShutdownHook(new Thread(()->{service.stopping=true;service.stop();}));
  new Thread(service::run,"LTE policy").start();Looper.loop();
 }
}
