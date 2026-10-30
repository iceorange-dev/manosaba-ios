// The first start's progress (plan item 301h): while the game's data an APK carries is copied out (android/data_apk.c), a dialog that cannot be
// dismissed shows how far it is. The native side calls these on the main thread (android/app_android.c).
package io.github.iceorange_dev.dm;

import android.app.Activity;
import android.app.AlertDialog;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

public final class Progress {
    private Progress() {}
    private static AlertDialog g_dialog;
    private static TextView g_text;
    private static ProgressBar g_bar;

    public static void show(Activity a, String title, String text) {
        if (g_dialog != null) return;
        LinearLayout col = new LinearLayout(a); col.setOrientation(LinearLayout.VERTICAL);
        int pad = Panel.dp(a, 24); col.setPadding(pad, Panel.dp(a, 8), pad, pad);
        g_text = new TextView(a); g_text.setText(text);
        g_bar = new ProgressBar(a, null, android.R.attr.progressBarStyleHorizontal); g_bar.setMax(1000);
        col.addView(g_text); col.addView(g_bar);
        g_dialog = new AlertDialog.Builder(a).setTitle(title).setView(col).setCancelable(false).show();
    }
    public static void update(String text, int permille) {   // permille of the work done
        if (g_dialog == null) return;
        g_text.setText(text); g_bar.setProgress(permille);
    }
    public static void done() {
        if (g_dialog != null) g_dialog.dismiss();
        g_dialog = null; g_text = null; g_bar = null;
    }
}
