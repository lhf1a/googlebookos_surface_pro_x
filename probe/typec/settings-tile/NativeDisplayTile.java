package org.surface.displaysettings;
import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
/** Metadata-only Settings dashboard tile. The controls remain inside Settings. */
public final class NativeDisplayTile extends Activity {
 @Override public void onCreate(Bundle state) {
  super.onCreate(state);
  startActivity(new Intent(android.provider.Settings.ACTION_DISPLAY_SETTINGS));
  finish();
 }
}
