package com.gtavrecomp.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * First screen of the public APK, which holds no game code and no game files. The player brings
 * two things, both made from their own copy of the game:
 *  - gtav-android-pack.zip from "Build for Android" in the PC launcher: their recompiled game
 *    library (libgtav_game.so), the shaders for this GPU and the known-good file hashes;
 *  - their discs: Disc 1 and Disc 2 (ISO images) and Title Update 27 (the TU file).
 * The pack goes to the app's private storage (the game library must be there: shared storage is
 * mounted noexec); the game files are copied from the discs and checked file by file into the
 * app's own folder (libgtav_setup.so, the same installer as on the PC and iPhone). When both are in
 * place, this screen starts the game directly.
 */
public class SetupActivity extends Activity {
    private static final String TAG = "GtavSetup";
    static {
        System.loadLibrary("gtav_setup");
    }

    private native String nativeInstall(String home, String expected, String disc1, String disc2, String tu);
    private static native boolean nativeIsDataInstalled(String home);
    private native String nativeAdopt(String home, String expected);

    private static final String[] LABELS = {
        "Game pack (gtav-android-pack.zip from Build for Android on your PC)",
        "Disc 1 (Install) image",
        "Disc 2 (Play) image",
        "Title Update 27 (tu00000009_00000000)",
    };
    private static final int PACK = 0;

    private final Uri[] picked = new Uri[4];
    private final TextView[] pickedText = new TextView[4];
    private final Button[] pickButtons = new Button[4];
    private TextView status, log;
    private ProgressBar progress;
    private Button install;
    private volatile boolean busy;
    private final AtomicBoolean cancelled = new AtomicBoolean(false);
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (!GameActivity.isReleaseLayout(this)) {
            // Developer APK (game code inside libmain.so, files pushed with adb): no setup.
            startGame();
            return;
        }
        addSetupShortcut();
        // "Run setup again" (long-press the app icon) opens this screen even when the game is
        // installed: to choose a newer pack (README: objects missing -> Build for Android again).
        boolean again = getIntent() != null && getIntent().getBooleanExtra(EXTRA_SETUP, false);
        if (!again && packCurrent() == null && nativeIsDataInstalled(home().getAbsolutePath())) {
            startGame();
            return;
        }
        for (int i = 0; i < 4; i++) {
            String s = prefs.getString("uri" + i, null);
            if (s != null) picked[i] = Uri.parse(s);
        }
        setContentView(buildUi());
        refresh();
    }

    private static final String EXTRA_SETUP = "com.gtavrecomp.app.SETUP";

    /** The launcher shortcut "Run setup again" (dynamic, so the APK needs no resources). */
    private void addSetupShortcut() {
        try {
            android.content.pm.ShortcutManager sm = getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return;
            Intent i = new Intent(this, SetupActivity.class)
                .setAction(Intent.ACTION_MAIN)
                .putExtra(EXTRA_SETUP, true)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
            android.content.pm.ShortcutInfo info = new android.content.pm.ShortcutInfo.Builder(this, "setup")
                .setShortLabel("Run setup again")
                .setLongLabel("Run setup again (new game pack)")
                .setIntent(i)
                .build();
            sm.setDynamicShortcuts(java.util.Collections.singletonList(info));
        } catch (Exception e) {
            Log.w(TAG, "shortcut: " + e);
        }
    }

    // --- state ----------------------------------------------------------------------------------

    /** Game data folder: the app's external files dir (data/{game,update,user}, spirv/, logs/). */
    private File home() {
        return getExternalFilesDir(null);
    }

    /** Game files copied over from a PC install (not yet checked). */
    private boolean copied() {
        File data = new File(home(), "data");
        return new File(data, "game/default.xex").exists() && new File(data, "update").isDirectory()
            && new File(data, "user").isDirectory()
            && !new File(data, "install_state.txt").exists();  // that is a disc install to resume
    }

    private File packDir() {
        return new File(getFilesDir(), "pack");
    }

    /** null when the installed pack fits this APK, else what is wrong (for the status line). */
    private String packCurrent() {
        File lib = new File(getFilesDir(), "libgtav_game.so");
        File info = new File(packDir(), "pack.txt");
        if (!lib.exists() || !info.exists()) return "No game pack yet.";
        String shell = readShell(info);
        String mine = sha256(new File(getApplicationInfo().nativeLibraryDir, "libgtav_fast.so"));
        if (shell == null || !shell.equals(mine))
            return "The game pack was made for another version of this app: run Build for Android "
                + "again with the download that has this APK.";
        return null;
    }

    private static String readShell(File info) {
        try (BufferedReader r = new BufferedReader(new FileReader(info))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("shell ")) return line.substring(6).trim();
            }
        } catch (Exception e) {
            Log.w(TAG, "pack.txt: " + e);
        }
        return null;
    }

    private static String sha256(File f) {
        try (InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) {
            return null;
        }
    }

    // --- UI -------------------------------------------------------------------------------------

    private View buildUi() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(Color.rgb(16, 20, 24));

        TextView title = text("GTA V (Xbox 360 recompilation)", 24);
        box.addView(title);
        box.addView(text("This app contains no game. It installs your own copy: make the game pack on "
            + "your PC (Build for Android in the PC launcher, after setting the game up there), copy "
            + "it and your disc images and title update to this phone (for example to Download), "
            + "choose them below and press Install. About 16 GB is copied and checked.", 14));
        box.addView(text("Short on space? Copy the game files from your PC instead of the discs: open this "
            + "app once, then copy the folders game, update and user from the data folder of your PC "
            + "install into " + new File(home(), "data") + " (by USB), choose only the game pack and "
            + "press Install. That needs about 16 GB instead of about 40.", 13));
        for (int i = 0; i < 4; i++) {
            final int slot = i;
            box.addView(text(LABELS[i], 15));
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            pickButtons[i] = new Button(this);
            pickButtons[i].setText("Choose...");
            pickButtons[i].setOnClickListener(v -> pick(slot));
            row.addView(pickButtons[i]);
            pickedText[i] = text("", 13);
            pickedText[i].setPadding(pad / 2, 0, 0, 0);
            row.addView(pickedText[i]);
            box.addView(row);
        }
        install = new Button(this);
        install.setText("Install");
        install.setOnClickListener(v -> onInstall());
        box.addView(install);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        box.addView(progress);
        status = text("", 15);
        box.addView(status);
        log = text("", 12);
        log.setTextColor(Color.rgb(150, 160, 170));
        box.addView(log);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(box);
        return scroll;
    }

    private TextView text(String s, int sp) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(Color.WHITE);
        t.setPadding(0, 8, 0, 4);
        return t;
    }

    private void refresh() {
        String pack = packCurrent();
        boolean data = nativeIsDataInstalled(home().getAbsolutePath());
        for (int i = 0; i < 4; i++) {
            String name = picked[i] != null ? displayName(picked[i]) : null;
            if (i == PACK && pack == null && picked[i] == null) name = "(installed)";
            if (i != PACK && data && picked[i] == null) name = "(game files installed)";
            else if (i != PACK && copied() && picked[i] == null) name = "(not needed: copied game files found)";
            pickedText[i].setText(name != null ? name : "not chosen");
            pickButtons[i].setEnabled(!busy);
        }
        install.setText(busy ? "Cancel" : "Install");
        if (!busy) {
            if (pack == null && data) status.setText("Ready.");
            else status.setText((pack != null ? pack + " " : "") + (data ? "" : copied()
                ? "Game files copied from a PC found: press Install to check them."
                : "Game files not installed yet."));
        }
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception e) {
            // the file was moved or deleted
        }
        return uri.getLastPathSegment();
    }

    private void pick(int slot) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, slot);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null || request < 0 || request > 3) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "persist " + uri + ": " + e);
        }
        picked[request] = uri;
        prefs.edit().putString("uri" + request, uri.toString()).apply();
        refresh();
    }

    // --- install --------------------------------------------------------------------------------

    private void onInstall() {
        if (busy) {
            cancelled.set(true);
            status.setText("Cancelling...");
            return;
        }
        cancelled.set(false);
        busy = true;
        log.setText("");
        refresh();
        new Thread(() -> {
            String error = runInstall();
            runOnUiThread(() -> {
                busy = false;
                refresh();
                if (error == null) {
                    // Installed: the discs are not needed again (they may be deleted to save space), so
                    // forget them like the pack; a later setup then checks copied files instead.
                    android.content.SharedPreferences.Editor e = prefs.edit();
                    for (int i = 1; i < 4; i++) {
                        e.remove("uri" + i);
                        picked[i] = null;
                    }
                    e.apply();
                    startGame();
                } else {
                    status.setText(cancelled.get() ? "Cancelled. Install again to continue where it stopped." : "Stopped.");
                    if (!cancelled.get()) message("Setup stopped", error);
                }
            });
        }, "gtav-setup").start();
    }

    /** On the install thread. null = done. */
    private String runInstall() {
        try {
            if (packCurrent() != null || picked[PACK] != null) {
                if (picked[PACK] == null) return packCurrent();
                String e = importPack(picked[PACK]);
                if (e != null) return e;
                String now = packCurrent();
                if (now != null) return now;
            }
            if (nativeIsDataInstalled(home().getAbsolutePath())) return null;
            // Copied from a PC (and no discs chosen): check those instead of installing from discs.
            if (copied() && picked[1] == null && picked[2] == null && picked[3] == null)
                return nativeAdopt(home().getAbsolutePath(), new File(packDir(), "expected.txt").getAbsolutePath());
            for (int i = 1; i < 4; i++) {
                if (picked[i] == null) return "Choose " + LABELS[i] + ".";
            }
            ParcelFileDescriptor[] fds = new ParcelFileDescriptor[4];
            try {
                String[] paths = new String[4];
                for (int i = 1; i < 4; i++) {
                    fds[i] = getContentResolver().openFileDescriptor(picked[i], "r");
                    if (fds[i] == null) return "Could not open " + displayName(picked[i]) + ".";
                    // The installer reads the open document like a file.
                    paths[i] = "/proc/self/fd/" + fds[i].getFd();
                }
                return nativeInstall(home().getAbsolutePath(), new File(packDir(), "expected.txt").getAbsolutePath(),
                                     paths[1], paths[2], paths[3]);
            } finally {
                for (ParcelFileDescriptor f : fds) {
                    if (f != null) try { f.close(); } catch (Exception ignored) { }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "install", e);
            return e.toString();
        }
    }

    /**
     * gtav-android-pack.zip -> files/libgtav_game.so (read-only), files/pack/{expected,pack}.txt,
     * <external>/spirv/. The game library is replaced only when the whole pack unpacked.
     */
    private String importPack(Uri uri) {
        onStage("Unpacking the game pack...");
        File stage = new File(getFilesDir(), "pack.new");
        deleteTree(stage);
        stage.mkdirs();
        File spirvNew = new File(home(), "spirv.new");
        deleteTree(spirvNew);
        spirvNew.mkdirs();
        boolean haveLib = false;
        int shaders = 0;
        byte[] buf = new byte[1 << 20];
        try (InputStream raw = getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                if (cancelled.get()) return "Cancelled.";
                String name = e.getName();
                if (e.isDirectory() || name.contains("..") || name.startsWith("/")) continue;
                File out;
                if (name.equals("libgtav_game.so")) {
                    out = new File(stage, name);
                    haveLib = true;
                } else if (name.equals("expected.txt") || name.equals("pack.txt")) {
                    out = new File(stage, name);
                } else if (name.startsWith("spirv/")) {
                    out = new File(spirvNew, name.substring(6));
                    shaders++;
                } else {
                    continue;
                }
                out.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buf)) > 0;) o.write(buf, 0, n);
                }
            }
        } catch (Exception ex) {
            return "Could not read the game pack: " + ex;
        }
        if (!haveLib || !new File(stage, "pack.txt").exists() || !new File(stage, "expected.txt").exists())
            return "This is not a game pack (gtav-android-pack.zip from Build for Android).";
        // Into place: the library first (read-only, as the system loader prefers), then the rest.
        File lib = new File(getFilesDir(), "libgtav_game.so");
        lib.delete();
        File staged = new File(stage, "libgtav_game.so");
        if (!staged.renameTo(lib)) return "Could not put the game library in place.";
        lib.setReadOnly();
        deleteTree(packDir());
        if (!stage.renameTo(packDir())) return "Could not put the game pack in place.";
        File spirv = new File(home(), "spirv");
        // (The shaders the phone meets later are saved in <external>/shader_cache, a separate folder
        // the next PC build reads back: not touched here.)
        deleteTree(spirv);
        if (!spirvNew.renameTo(spirv)) return "Could not put the shaders in place.";
        onLog("Game pack: game library and " + shaders + " shaders installed.");
        prefs.edit().remove("uri0").apply();
        picked[PACK] = null;
        return null;
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private void message(String title, String text) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(text).setPositiveButton("OK", null).show();
    }

    // --- called by libgtav_setup.so on the install thread (InstallUi) ----------------------------

    void onStage(String s) {
        runOnUiThread(() -> status.setText(s));
    }

    void onLog(String s) {
        runOnUiThread(() -> {
            CharSequence old = log.getText();
            String all = old + (old.length() > 0 ? "\n" : "") + s;
            // The last lines only: the full log is data/logs/install.log.
            String[] lines = all.split("\n");
            int from = Math.max(0, lines.length - 12);
            StringBuilder sb = new StringBuilder();
            for (int i = from; i < lines.length; i++) sb.append(i > from ? "\n" : "").append(lines[i]);
            log.setText(sb);
        });
    }

    void onProgress(double f) {
        runOnUiThread(() -> progress.setProgress((int) (f * 1000)));
    }

    boolean isCancelled() {
        return cancelled.get();
    }

    boolean onAsk(String question) {
        CountDownLatch done = new CountDownLatch(1);
        boolean[] yes = {false};
        runOnUiThread(() -> new AlertDialog.Builder(this).setMessage(question).setCancelable(false)
            .setPositiveButton("Yes", (d, w) -> { yes[0] = true; done.countDown(); })
            .setNegativeButton("No", (d, w) -> done.countDown()).show());
        try {
            done.await();
        } catch (InterruptedException e) {
            return false;
        }
        return yes[0];
    }
}
