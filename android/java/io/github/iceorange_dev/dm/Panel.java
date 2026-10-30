// The settings panel of the Android app (plan item 301g), the iPhone's in-game panel (ios/Sources/front_ios.m) in Android's own widgets: Back or a
// three-finger tap opens it (android/front_android.c), the app's native code calls show() on the main thread (android/app_android.c). Each change is
// written at once to the settings file in the app's own folder (NAME=value lines, the switches env.txt takes: android/settings.c reads it at the
// start, env.txt winning) and handed to the native side (applied()), which applies the frame rate while the game runs; the render resolution is
// the game's choice at its start, so it takes effect at the next launch, as on the iPhone. The frame rate line (perf()) sits in the top right
// corner in a window of its own that takes no touches, updated twice a second from the native side (perfLine(), gfx/nvn's dm_perf_line).
package io.github.iceorange_dev.dm;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

public final class Panel {
    private Panel() {}
    static native void applied();      // the settings file changed (app_android.c registers both)
    static native String perfLine();   // the fps / GPU line

    // ---------- the settings file ----------
    static Map<String, String> load(String path) {
        Map<String, String> m = new LinkedHashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            for (String line; (line = r.readLine()) != null; ) {
                int eq = line.indexOf('='); if (eq <= 0 || line.trim().startsWith("#")) continue;
                m.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        } catch (IOException e) { /* no file yet: every setting at its default */ }
        return m;
    }
    static void save(String path, Map<String, String> m) {   // a whole new file, then renamed over the old one
        File tmp = new File(path + ".tmp");
        try (FileWriter w = new FileWriter(tmp)) {
            w.write("# The settings panel's (Back or three fingers in the game); env.txt in the external folder wins over these\n");
            for (Map.Entry<String, String> e : m.entrySet()) w.write(e.getKey() + "=" + e.getValue() + "\n");
        } catch (IOException e) { return; }
        if (!tmp.renameTo(new File(path))) tmp.delete();
    }
    static void set(String path, Map<String, String> m, String key, String value) {
        if (value.isEmpty()) m.remove(key); else m.put(key, value);
        save(path, m); applied();
    }

    // ---------- the panel ----------
    static int dp(Activity a, float v) { return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, a.getResources().getDisplayMetrics())); }
    static View row(Activity a, String title, View control) {
        LinearLayout r = new LinearLayout(a); r.setOrientation(LinearLayout.VERTICAL); r.setPadding(0, dp(a, 8), 0, dp(a, 8));
        TextView t = new TextView(a); t.setText(title); t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        r.addView(t); r.addView(control); return r;
    }
    // One of several values (the iPhone's segmented control): an empty value is the setting left out (its default)
    static View choice(Activity a, String path, Map<String, String> m, String key, String[] titles, String[] values, String dflt) {
        RadioGroup g = new RadioGroup(a); g.setOrientation(RadioGroup.HORIZONTAL);
        String cur = m.containsKey(key) ? m.get(key) : dflt;
        for (int i = 0; i < titles.length; i++) {
            RadioButton b = new RadioButton(a); b.setText(titles[i]); b.setId(View.generateViewId());
            g.addView(b); if (values[i].equals(cur)) b.setChecked(true);
            final String v = values[i];
            b.setOnCheckedChangeListener((CompoundButton x, boolean on) -> { if (on) set(path, m, key, v.equals(dflt) ? "" : v); });
        }
        return g;
    }
    static View toggle(Activity a, String path, Map<String, String> m, String key, boolean dflt) {
        Switch s = new Switch(a); String cur = m.get(key);
        s.setChecked(cur == null ? dflt : !cur.equals("0"));
        s.setOnCheckedChangeListener((CompoundButton x, boolean on) -> set(path, m, key, on == dflt ? "" : on ? "1" : "0"));
        return s;
    }
    public static void show(Activity a, String path) {
        Map<String, String> m = load(path);
        LinearLayout col = new LinearLayout(a); col.setOrientation(LinearLayout.VERTICAL); col.setPadding(dp(a, 24), dp(a, 8), dp(a, 24), 0);
        col.addView(row(a, "Frame rate", choice(a, path, m, "DM_FPS", new String[] {"60", "30"}, new String[] {"60", "30"}, "60")));
        col.addView(row(a, "Hold 30 fps when hot", toggle(a, path, m, "DM_HOT_HOLD", true)));
        col.addView(row(a, "Render resolution (next launch)", choice(a, path, m, "DM_RENDER_HEIGHT", new String[] {"Game", "Native", "900", "720", "540"},
                                                                   new String[] {"", "native", "900", "720", "540"}, "")));
        col.addView(row(a, "Show frame rate", toggle(a, path, m, "DM_PERF_LINE", false)));
        ScrollView sv = new ScrollView(a); sv.addView(col);
        new AlertDialog.Builder(a).setTitle("Settings").setView(sv).setPositiveButton("Done", null).show();
    }

    // The screen's pixels (WindowManager.getMaximumWindowMetrics, API 30): what the setting Native fits its 16:9 picture into
    public static int[] displaySize(Activity a) {
        Rect r = a.getWindowManager().getMaximumWindowMetrics().getBounds();
        return new int[] {r.width(), r.height()};
    }

    // ---------- the frame rate line ----------
    private static final Handler g_main = new Handler(Looper.getMainLooper());
    private static PopupWindow g_perf;
    private static TextView g_perfText;
    public static void perf(Activity a, boolean on) {
        if (!on) { if (g_perf != null) g_perf.dismiss(); g_perf = null; return; }
        if (g_perf != null) return;
        View decor = a.getWindow().getDecorView();
        decor.post(() -> {
            if (g_perf != null) return;
            TextView t = new TextView(a); t.setTextColor(Color.WHITE); t.setBackgroundColor(0x99000000); t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            t.setPadding(dp(a, 6), dp(a, 2), dp(a, 6), dp(a, 2));
            PopupWindow w = new PopupWindow(t, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, false);
            w.setTouchable(false); w.setFocusable(false);
            try { w.showAtLocation(decor, Gravity.TOP | Gravity.END, dp(a, 8), dp(a, 8)); }
            catch (RuntimeException e) { g_main.postDelayed(() -> perf(a, true), 500); return; }   // the window not shown yet: again shortly
            g_perf = w; g_perfText = t; tick();
        });
    }
    private static void tick() {
        if (g_perf == null) return;
        g_perfText.setText(perfLine());
        g_main.postDelayed(Panel::tick, 500);
    }
}
