package dev.melee;

import org.libsdl.app.SDLActivity;

public class MeleeActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        /* libpng is linked statically since the aurora sync to 77326d45
         * (extern/aurora/extern/CMakeLists.txt, _USE_SHARED is off on
         * Android), so a clean build ships no libpng16.so and loading it
         * failed before the activity had a window. */
        return new String[] {
            "melee"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    public org.libsdl.app.SDLSurface createSDLSurface(android.content.Context context) {
        return new dev.encounter.aurora.AuroraSurface(context);
    }

    private static final int EXPORT_LOGS_REQUEST = 7124;
    private static final int DISC_PICK_REQUEST = 7125;

    /* The startup disc menu, called from native before the launcher runs
     * (src/pc/android_compat.cpp, pc_android_startup_disc). The launcher is an
     * RmlUi page drawn on the SDL surface: TalkBack cannot read it and only a
     * sighted tap can move its focus, so on a phone the disc is chosen here,
     * in a real dialog that Android's own accessibility already reads. The
     * game's speech takes over as soon as it boots. */
    private final Object mDiscMenuLock = new Object();
    private boolean mDiscMenuDone;
    private String mDiscMenuChoice;
    private String mDiscMenuSaved = "";
    private android.app.AlertDialog mDiscMenu;
    private android.net.Uri mDiscMenuPicked;
    private volatile long mDiscMenuShownAt;

    /* Runs on the native thread and blocks it until the dialog is answered:
     * the launcher must not draw underneath it. Returns the image to boot, or
     * null to fall through to the launcher. */
    public String showStartupDiscMenu(String savedDisc) {
        final String saved = savedDisc == null ? "" : savedDisc;
        synchronized (mDiscMenuLock) {
            mDiscMenuSaved = saved;
            mDiscMenuPicked = null;
            mDiscMenuChoice = null;
            mDiscMenuShownAt = 0;
            mDiscMenuDone = false;
        }
        runOnUiThread(() -> {
            try {
                showStartupDiscDialog();
            } catch (Exception e) {
                /* No dialog (a finishing activity, say): behave as Launcher. */
                finishStartupDiscMenu(null);
            }
        });
        final long postedAt = System.currentTimeMillis();
        synchronized (mDiscMenuLock) {
            while (!mDiscMenuDone) {
                /* Never wait forever on a dialog that never appeared. */
                if (mDiscMenuShownAt == 0 && System.currentTimeMillis() - postedAt > 20000) {
                    mDiscMenuChoice = null;
                    break;
                }
                try {
                    mDiscMenuLock.wait(1000);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            return mDiscMenuChoice;
        }
    }

    private void showStartupDiscDialog() {
        mDiscMenu = new android.app.AlertDialog.Builder(this)
            .setTitle("Melee PC")
            .setMessage(startupDiscMessage())
            .setPositiveButton("Start", (d, w) -> finishStartupDiscMenu(startupDiscChoice()))
            .setNeutralButton("Choose disc image", null)
            .setNegativeButton("Launcher", (d, w) -> finishStartupDiscMenu(null))
            .setOnCancelListener(d -> finishStartupDiscMenu(null))
            .create();
        mDiscMenu.show();
        mDiscMenuShownAt = System.currentTimeMillis();
        mDiscMenu.getButton(android.app.AlertDialog.BUTTON_POSITIVE)
            .setEnabled(!mDiscMenuSaved.isEmpty());
        /* A neutral button dismisses by default: this one opens the picker and
         * leaves the dialog up, so the image can be checked before Start. */
        mDiscMenu.getButton(android.app.AlertDialog.BUTTON_NEUTRAL)
            .setOnClickListener(v -> startDiscPicker());
    }

    private String startupDiscChoice() {
        if (mDiscMenuPicked != null) {
            return mDiscMenuPicked.toString();
        }
        return mDiscMenuSaved.isEmpty() ? null : mDiscMenuSaved;
    }

    private String startupDiscMessage() {
        String name = "";
        if (mDiscMenuPicked != null) {
            String segment = mDiscMenuPicked.getLastPathSegment();
            name = segment != null ? segment : mDiscMenuPicked.toString();
        } else if (!mDiscMenuSaved.isEmpty()) {
            try {
                String segment = android.net.Uri.parse(mDiscMenuSaved).getLastPathSegment();
                name = segment != null ? segment : mDiscMenuSaved;
            } catch (Exception e) {
                name = mDiscMenuSaved;
            }
        }
        if (name.isEmpty()) {
            return "No disc image chosen yet. Choose your Melee USA revision 2 image "
                + "(GALE01: .iso, .gcm, .ciso, .rvz, .gcz or .wia) to start.";
        }
        return "Disc image: " + name
            + "\n\nThe game speaks from here. Press Start to boot it.";
    }

    private void startDiscPicker() {
        android.content.Intent intent = new android.content.Intent(
            android.content.Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
            | android.content.Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, DISC_PICK_REQUEST);
        } catch (android.content.ActivityNotFoundException e) {
            android.widget.Toast.makeText(this, "No file picker is available.",
                android.widget.Toast.LENGTH_LONG).show();
        }
    }

    /* The picker's grant lasts for this session, so keep it: the next launch
     * offers the same image as a one-tap Start. */
    private void finishedDiscPick(android.content.Intent data) {
        final android.net.Uri uri = data.getData();
        if (uri == null) {
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(uri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
        mDiscMenuPicked = uri;
        if (mDiscMenu != null) {
            mDiscMenu.setMessage(startupDiscMessage());
            mDiscMenu.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setEnabled(true);
        }
    }

    private void finishStartupDiscMenu(String choice) {
        synchronized (mDiscMenuLock) {
            if (mDiscMenuDone) {
                return;
            }
            mDiscMenuChoice = choice;
            mDiscMenuDone = true;
            mDiscMenuLock.notifyAll();
        }
        if (mDiscMenu != null && mDiscMenu.isShowing()) {
            mDiscMenu.dismiss();
        }
    }

    public void exportLogs() {
        runOnUiThread(() -> {
            android.content.Intent intent = new android.content.Intent(
                android.content.Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(android.content.Intent.EXTRA_TITLE, "melee-pc-diagnostics.txt");
            try {
                startActivityForResult(intent, EXPORT_LOGS_REQUEST);
            } catch (android.content.ActivityNotFoundException e) {
                android.widget.Toast.makeText(this, "No document picker is available.",
                    android.widget.Toast.LENGTH_LONG).show();
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == DISC_PICK_REQUEST) {
            if (resultCode == RESULT_OK && data != null) {
                finishedDiscPick(data);
            }
            return;
        }
        if (requestCode != EXPORT_LOGS_REQUEST || resultCode != RESULT_OK || data == null ||
                data.getData() == null) {
            return;
        }
        final android.net.Uri destination = data.getData();
        new Thread(() -> {
            String result = "Diagnostics saved. Attach this file to your issue.";
            Process capture = null;
            try (java.io.OutputStream output = getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) {
                    throw new java.io.IOException("The selected document could not be opened.");
                }
                String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
                String header = "Melee PC " + version + "\nDevice: " + android.os.Build.MANUFACTURER +
                    " " + android.os.Build.MODEL + "\nAndroid: " + android.os.Build.VERSION.RELEASE +
                    " (API " + android.os.Build.VERSION.SDK_INT + ")\nABI: " +
                    java.util.Arrays.toString(android.os.Build.SUPPORTED_ABIS) +
                    "\nRecent app logs (including previous launches still in the log buffer):\n\n";
                output.write(header.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                // Android permits reading this app's own UID without READ_LOGS.
                // Keep previous-process messages, and exclude other apps' logs.
                capture = new ProcessBuilder("logcat", "-d", "-v", "threadtime",
                    "--uid=" + android.os.Process.myUid(), "Aurora:V", "Melee:V", "melee:V",
                    "AndroidRuntime:V", "libc:V", "*:S").redirectErrorStream(true).start();
                try (java.io.InputStream input = capture.getInputStream()) {
                    byte[] buffer = new byte[8192];
                    int remaining = 4 * 1024 * 1024;
                    int count;
                    while (remaining > 0 && (count = input.read(buffer, 0,
                            Math.min(buffer.length, remaining))) != -1) {
                        output.write(buffer, 0, count);
                        remaining -= count;
                    }
                    if (remaining == 0) {
                        capture.destroy();
                    } else if (capture.waitFor() != 0) {
                        throw new java.io.IOException("Android could not read the app log buffer.");
                    }
                }
            } catch (Exception e) {
                result = "Could not export diagnostics: " + e.getMessage();
            } finally {
                if (capture != null) {
                    capture.destroy();
                }
            }
            final String message = result;
            runOnUiThread(() -> android.widget.Toast.makeText(this, message,
                android.widget.Toast.LENGTH_LONG).show());
        }, "melee-log-export").start();
    }

    private TouchOverlayView mTouchOverlay;
    private boolean mFullscreen = true;
    private MeleeSpeech mSpeech;
    private String mA11ySoundsDir;

    /* Called from native (src/pc/a11y/android_tts.cpp) on the game thread or
     * the voices' worker: MeleeSpeech posts its own engine calls to the main
     * thread, so neither needs this one. */
    public MeleeSpeech getSpeech() {
        if (mSpeech == null) {
            mSpeech = MeleeSpeech.get(this);
        }
        return mSpeech;
    }

    /* Called from native (src/pc/android_compat.cpp) with a disc image the
     * launcher saved as a content:// URI: the picker grants read access for
     * this session, so it has to be persisted for the next launch to open
     * the same disc. Providers that offer no persistable grant throw; the
     * image still opens now, and Choose Disc fixes it next time. */
    public void takePersistableUri(String uri) {
        try {
            getContentResolver().takePersistableUriPermission(
                android.net.Uri.parse(uri),
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
    }

    /* The accessibility cues' sound files, unpacked from the APK on first
     * ask (src/pc/a11y/cue_output.cpp reads them from a folder). Null when
     * the assets cannot be read, which leaves the cues silent. */
    public String getA11ySoundsDir() {
        if (mA11ySoundsDir == null) {
            mA11ySoundsDir = unpackA11ySounds();
        }
        return mA11ySoundsDir;
    }

    private String unpackA11ySounds() {
        java.io.File directory = new java.io.File(getFilesDir(), "a11y");
        if (!directory.isDirectory() && !directory.mkdirs()) {
            return null;
        }
        try {
            String[] names = getAssets().list("a11y");
            if (names == null) {
                return null;
            }
            for (String name : names) {
                java.io.File target = new java.io.File(directory, name);
                if (target.isFile() && target.length() > 0) {
                    continue; /* already unpacked by an earlier run */
                }
                try (java.io.InputStream in = getAssets().open("a11y/" + name);
                        java.io.OutputStream out = new java.io.FileOutputStream(target)) {
                    byte[] buffer = new byte[8192];
                    int count;
                    while ((count = in.read(buffer)) > 0) {
                        out.write(buffer, 0, count);
                    }
                }
            }
        } catch (java.io.IOException e) {
            return null;
        }
        return directory.getAbsolutePath() + "/";
    }

    public void setFullscreen(final boolean fullscreen) {
        runOnUiThread(() -> {
            mFullscreen = fullscreen;
            applyImmersiveMode();
        });
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) {
            return;
        }
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        applyImmersiveMode();

        mTouchOverlay = new TouchOverlayView(this);
        if (mLayout != null) {
            mLayout.addView(mTouchOverlay, new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            ));
        }

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.N) {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(android.content.Context.POWER_SERVICE);
            if (pm != null && pm.isSustainedPerformanceModeSupported()) {
                getWindow().setSustainedPerformanceMode(true);
            }
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = 60.0f;
            getWindow().setAttributes(lp);
        }
        /* Only pre-Android 13 can be granted READ/WRITE_EXTERNAL_STORAGE at
         * all; from API 33 they are not grantable and the request resolves
         * instantly, but the dialog still pauses and recreates the activity
         * while the graphics device is coming up, which killed the process at
         * startup on a Pixel 8. The disc is read from the app's own external
         * files dir, which needs no permission on any version. */
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M
                && android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 100);
            }
        }
    }

    /* Called from native (src/pc/android_compat.cpp) around pc_launcher_run:
     * the launcher needs every touch while it is on screen. */
    public void setLauncherActive(final boolean active) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mTouchOverlay != null) {
                    mTouchOverlay.setLauncherActive(active);
                }
            }
        });
    }

    public static native void nativeDisconnect();

    @Override
    protected void onStop() {
        if (isFinishing()) {
            try {
                nativeDisconnect();
            } catch (Throwable ignored) {}
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        try {
            nativeDisconnect();
        } catch (Throwable ignored) {}
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = 60.0f;
            getWindow().setAttributes(lp);
        }
        if (mTouchOverlay != null) {
            mTouchOverlay.updateControllerState();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        // Insets/IMMERSIVE_STICKY handle swipes; SDL's delayed hide ignores our saved choice.
    }

    private void applyImmersiveMode() {
        if (mBrokenLibraries) {
            return;
        }
        android.view.Window window = getWindow();
        if (window == null) {
            return;
        }
        window.setFlags(mFullscreen ? android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN : 0,
            android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(!mFullscreen);
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                if (mFullscreen)
                    controller.hide(android.view.WindowInsets.Type.systemBars());
                else
                    controller.show(android.view.WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                    mFullscreen ? android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        : android.view.WindowInsetsController.BEHAVIOR_DEFAULT);
            }
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                android.view.WindowManager.LayoutParams lp = window.getAttributes();
                lp.layoutInDisplayCutoutMode =
                    mFullscreen ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                        : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
                window.setAttributes(lp);
            }
        } else {
            int flags = android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
            window.getDecorView().setSystemUiVisibility(mFullscreen ? flags
                : android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    protected String[] getArguments() {
        android.content.Intent intent = getIntent();
        if (intent != null) {
            String[] args = intent.getStringArrayExtra("args");
            if (args != null && args.length > 0) {
                return args;
            }
            String disc = intent.getStringExtra("disc");
            if (disc != null && !disc.isEmpty()) {
                return new String[] { "--dvd", disc };
            }
            android.net.Uri data = intent.getData();
            if (data != null) {
                try {
                    getContentResolver().takePersistableUriPermission(
                        data, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (Exception ignored) {
                }
                return new String[] { "--dvd", data.toString() };
            }
        }
        return new String[0];
    }

}
