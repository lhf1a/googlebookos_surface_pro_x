package android.net;
import android.content.Context;
import android.os.Looper;
public abstract class NetworkAgent {
 public NetworkAgent(Context c,Looper l,String t,NetworkCapabilities n,LinkProperties p,int s,NetworkAgentConfig a,NetworkProvider v){}
 public void onNetworkUnwanted(){}
 public void onValidationStatus(int status,Uri uri){}
}
