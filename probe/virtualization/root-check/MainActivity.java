package org.surface.sq2.rootcheck;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/** Explicit, temporary app authorization check. Only executes id and id -Z. */
public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 80, 32, 32);
        TextView result = new TextView(this);
        result.setTextSize(20);
        result.setText("App UID: " + android.os.Process.myUid() +
                       "\nRead-only command: su -c 'id; id -Z'\nNo files or settings are changed by root.");
        Button check = new Button(this);
        check.setText("Request root and check identity");
        layout.addView(result);
        layout.addView(check);
        setContentView(layout);
        check.setOnClickListener(view -> {
            check.setEnabled(false);
            result.setText("App UID: " + android.os.Process.myUid() + "\nWaiting for authorization (60 seconds)…");
            new Thread(() -> {
                String text;
                Process child = null;
                try {
                    File output = new File(getFilesDir(), "identity.txt");
                    child = new ProcessBuilder("su", "-c", "id; id -Z")
                            .redirectErrorStream(true).redirectOutput(output).start();
                    if (!child.waitFor(60, TimeUnit.SECONDS)) {
                        child.destroyForcibly();
                        text = "TIMEOUT: no completed root identity check";
                    } else {
                        byte[] data = new byte[4096];
                        int count;
                        try (FileInputStream input = new FileInputStream(output)) {
                            count = input.read(data);
                        }
                        String identity = count < 0 ? "" : new String(data, 0, count, StandardCharsets.UTF_8);
                        boolean root = child.exitValue() == 0 && identity.startsWith("uid=0(");
                        text = (root ? "ROOT_IDENTITY_PASS" : "ROOT_NOT_GRANTED") +
                               "\nexit=" + child.exitValue() + "\n" + identity;
                    }
                } catch (Exception error) {
                    text = "ROOT_CHECK_ERROR: " + error;
                } finally {
                    if (child != null && child.isAlive()) child.destroyForcibly();
                }
                final String completed = "App UID: " + android.os.Process.myUid() + "\n" + text;
                runOnUiThread(() -> { result.setText(completed); check.setEnabled(true); });
            }, "root-identity-check").start();
        });
    }
}
