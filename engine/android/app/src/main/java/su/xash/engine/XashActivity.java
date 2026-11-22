package su.xash.engine;

import android.annotation.SuppressLint;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings.Secure;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.DisplayCutout;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import su.xash.engine.util.CrashReports;
import su.xash.engine.util.Prefs;
import su.xash.engine.util.SessionLog;
import su.xash.engine.util.SoftKeyboardPan;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Arrays;
import java.util.List;

public class XashActivity extends SDLActivity {
	private boolean mUseVolumeKeys;
	private volatile float[] mWindowInsets = new float[4];
	private String mPackageName;
	private static final String TAG = "XashActivity";

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
			//getWindow().addFlags(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES);
			getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
		}

		SoftKeyboardPan.assistActivity(this);

		// mSurface is null if SDL failed to start and shows its error dialog
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && mSurface != null) {
			mSurface.setOnApplyWindowInsetsListener((view, insets) -> {
				updateWindowInsets(view, insets);
				return insets;
			});
			mSurface.addOnLayoutChangeListener((view, l, t, r, b, ol, ot, or, ob) ->
				updateWindowInsets(view, view.getRootWindowInsets()));
			mSurface.requestApplyInsets();
		}
	}

	// JNI reads a snapshot; all View access stays on Android's UI thread.
	private float[] getWindowInsets() {
		return mWindowInsets;
	}

	private void updateWindowInsets(View surface, WindowInsets insets) {
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P)
			return;

		if (insets == null || surface.getWidth() <= 0 || surface.getHeight() <= 0)
			return;

		// Only the display cutout: system bars are hidden in immersive mode and reporting them while they're swiped in would shift the whole layout.
		int left = 0, top = 0, right = 0, bottom = 0;
		DisplayCutout cutout = insets.getDisplayCutout();
		if (cutout != null) {
			left = cutout.getSafeInsetLeft();
			top = cutout.getSafeInsetTop();
			right = cutout.getSafeInsetRight();
			bottom = cutout.getSafeInsetBottom();
		}
		float width = surface.getWidth(), height = surface.getHeight();
		mWindowInsets = new float[] { left / width, top / height, right / width, bottom / height };
	}

	@Override
	public void onDestroy() {
		super.onDestroy();

		// half-D v0.8.2: archive this session's console log plus a logcat
		// dump before the hard exit. The engine only writes engine.log;
		// native stdout and GPU-driver messages live in the logcat ring
		// buffer and were lost on every abnormal exit — the crash pipeline
		// also dumps it on the launch after a crash, but a clean session
		// archive keeps every run inspectable and shareable.
		try {
			SessionLog.archiveSession(this, new File(Prefs.getGamePath(this), "engine.log"));
		} catch (Throwable t) {
			Log.w(TAG, "session log archive failed: " + t);
		}

		// Now that we don't exit from native code, we need to exit here, resetting
		// application state (actually global variables that we don't cleanup on exit)
		//
		// When the issue with global variables will be resolved, remove that exit() call
		System.exit(0);
	}

	@Override
	protected String[] getLibraries() {
		return new String[]{"SDL2", "xash"};
	}

	@SuppressLint("HardwareIds")
	private String getAndroidID() {
		return Secure.getString(getContentResolver(), Secure.ANDROID_ID);
	}

	@SuppressLint("ApplySharedPref")
	private void saveAndroidID(String id) {
		getSharedPreferences("xash_preferences", MODE_PRIVATE).edit().putString("xash_id", id).commit();
	}

	private String loadAndroidID() {
		return getSharedPreferences("xash_preferences", MODE_PRIVATE).getString("xash_id", "");
	}

	@Override
	public String getCallingPackage() {
		if (mPackageName != null) {
			return mPackageName;
		}

		return super.getCallingPackage();
	}

	private AssetManager getAssets(boolean isEngine) {
		AssetManager am = null;

		if (isEngine) {
			am = getAssets();
		} else {
			try {
				am = getPackageManager().getResourcesForApplication(getCallingPackage()).getAssets();
			} catch (Exception e) {
				Log.e(TAG, "Unable to load mod assets!");
				e.printStackTrace();
			}
		}

		return am;
	}

	private String[] getAssetsList(boolean isEngine, String path) {
		AssetManager am = getAssets(isEngine);

		try {
			return am.list(path);
		} catch (Exception e) {
			e.printStackTrace();
		}

		return new String[]{};
	}

	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		if (SDLActivity.mBrokenLibraries) {
			return false;
		}

		int keyCode = event.getKeyCode();
		if (!mUseVolumeKeys) {
			if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_CAMERA || keyCode == KeyEvent.KEYCODE_ZOOM_IN || keyCode == KeyEvent.KEYCODE_ZOOM_OUT) {
				return false;
			}
		}

		return getWindow().superDispatchKeyEvent(event);
	}

	private static void appendStringExtra(StringBuilder sb, Intent intent, String key) {
		String value = intent.getStringExtra(key);
		if (value != null)
			sb.append("  ").append(key).append(" = ").append(value).append('\n');
	}

	// record intent info, so that it could be consumed later for crash reporting
	private void recordLaunchInfo() {
		// do not overwrite current launch info with pending crash log, shouldn't happen but might
		File pendingCrash = new File(getFilesDir(), "crashes/" + CrashReports.STACKTRACE_NAME);
		if (pendingCrash.exists() && pendingCrash.length() > 0)
			return;

		// write Android version, fingerprint, supported abis, etc
		CrashReports.writeSystemInfo(this);

		// now create intent info and pass it to crash reporting
		Intent intent = getIntent();
		if (intent == null)
			return;
		StringBuilder sb = new StringBuilder();
		sb.append("Action: ").append(intent.getAction()).append('\n');
		sb.append("Data: ").append(intent.getDataString()).append('\n');
		sb.append("Calling package: ").append(getCallingPackage()).append('\n');
		sb.append("Extras:\n");
		// only write intent extras that we care about
		appendStringExtra(sb, intent, "gamedir");
		appendStringExtra(sb, intent, "gamelibdir");
		appendStringExtra(sb, intent, "pakfile");
		appendStringExtra(sb, intent, "basedir");
		appendStringExtra(sb, intent, "package");
		appendStringExtra(sb, intent, "argv");
		sb.append("  usevolume = ").append(intent.getBooleanExtra("usevolume", false)).append('\n');
		String[] env = intent.getStringArrayExtra("env");
		if (env != null)
			sb.append("  env = ").append(Arrays.toString(env)).append('\n');
		CrashReports.writeIntentInfo(this, sb.toString());
	}

	// TODO: REMOVE LATER, temporary launchers support?
	@Override
	protected String[] getArguments() {
		File crashDir = new File(getFilesDir(), "crashes");
		crashDir.mkdirs();
		nativeSetenv("XASH3D_CRASH_DIR", crashDir.getAbsolutePath());

		recordLaunchInfo();

		String gamedir = getIntent().getStringExtra("gamedir");
		if (gamedir == null) gamedir = "valve";
		nativeSetenv("XASH3D_GAME", gamedir);

		String gamelibdir = getIntent().getStringExtra("gamelibdir");
		if (gamelibdir != null) nativeSetenv("XASH3D_GAMELIBDIR", gamelibdir);

		String rodir = System.getenv("XASH3D_RODIR");
		if (rodir == null) {
			// FIXME: we are using rodir as a supplier for downloaded game libraries
			rodir = getFilesDir().getAbsolutePath() + "/gamelibs";
			nativeSetenv("XASH3D_RODIR", rodir);
		}
		Log.i(TAG, "XASH3D_RODIR = " + rodir);

		// half-D v0.8.2: auto-apply the launcher's tuned device config
		extractUserconfig(rodir, gamedir);

		// half-D: MainUI loads the menu font from the game's search paths
		// (gfx/fonts/FiraSans-Regular.ttf) and the itch download doesn't ship
		// it. Bundle it in the APK assets and extract it into the read-only
		// game dir the engine already searches (<rodir>/<gamedir>/...).
		// FiraSans is SIL OFL (assets/fonts/OFL.txt in the source tree).
		try {
			File fontDir = new File(rodir, gamedir + "/gfx/fonts");
			File fontFile = new File(fontDir, "FiraSans-Regular.ttf");
			if (!fontFile.exists() || fontFile.length() == 0) {
				fontDir.mkdirs();
				try (InputStream in = getAssets().open("gfx/fonts/FiraSans-Regular.ttf");
				     FileOutputStream out = new FileOutputStream(fontFile)) {
					byte[] buf = new byte[65536];
					int n;
					while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
				}
				Log.i(TAG, "Extracted menu font to " + fontFile.getAbsolutePath());
			}
		} catch (Exception e) {
			Log.w(TAG, "Could not extract menu font: " + e);
		}

		String pakfile = getIntent().getStringExtra("pakfile");
		if (pakfile != null) nativeSetenv("XASH3D_EXTRAS_PAK2", pakfile);

		// Not started from the library, use the location from settings
		String basedir = getIntent().getStringExtra("basedir");
		if (basedir == null) {
			basedir = Prefs.getGamePath(this).getAbsolutePath();
		}
		nativeSetenv("XASH3D_BASEDIR", basedir);

		mUseVolumeKeys = getIntent().getBooleanExtra("usevolume", false);
		mPackageName = getIntent().getStringExtra("package");

		String[] env = getIntent().getStringArrayExtra("env");
		if (env != null) {
			for (int i = 0; i < env.length; i += 2)
				nativeSetenv(env[i], env[i + 1]);
		}

		String argv = getIntent().getStringExtra("argv");
		// half-D v0.7.1: no hardcoded renderer on the command line. The
		// engine's ref_common.c defaults to gl4es (the proven texture path
		// for this game's DDS data). half-D v0.7.3: the Diffusion custom
		// renderer is the default again (gl_diffusion_renderer 1) — the
		// v0.7.0-0.7.2 engine-renderer mode drew the game without its
		// materials/lighting/effects. -ref gles3compat stays selectable
		// for experiments.
		// half-D v0.7.4: -ref gl4es is FORCED on the default command line.
		// ref_dll is FCVAR_RENDERINFO (saved to opengl.cfg), so a ref picked
		// by an earlier build — v0.7.0 defaulted gles3compat, and
		// gles3compat + custom renderer is the v0.5.0 field configuration:
		// garbage textures at 0-4 fps — could silently win over new code
		// defaults. Command line has top priority; this pins the proven ref.
		if (argv == null) argv = "-console -log -dev 2 -ref gl4es";

		return argv.split(" ");
	}

	// half-D v0.8.2: auto-apply the launcher's tuned device config. The
	// bundled assets/cfg/userconfig.cfg is extracted into the read-only
	// game dir (<rodir>/<gamedir>/userconfig.cfg); the engine's search
	// hierarchy ranks rodir above the writable basedir, so the console
	// exec of userconfig.cfg applies the launcher's patch level even when
	// the game folder carries an older copy. Written once per config
	// version stamp (first line): user edits survive until the next app
	// update raises the version.
	private void extractUserconfig(String rodir, String gamedir) {
		try (InputStream in = getAssets().open("cfg/userconfig.cfg")) {
			java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
			byte[] buf = new byte[65536];
			int n;
			while ((n = in.read(buf)) > 0)
				bos.write(buf, 0, n);
			String wanted = bos.toString("UTF-8");

			// the version stamp is the first line of the bundled file
			int nl = wanted.indexOf('\n');
			String wantedVersion = nl >= 0 ? wanted.substring(0, nl) : wanted;
			File cfgFile = new File(rodir, gamedir + "/userconfig.cfg");
			if (cfgFile.exists() && cfgFile.length() > 0) {
				// plain FileInputStream, not java.nio.file.Files — minSdk is 21
				java.io.ByteArrayOutputStream haveBuf = new java.io.ByteArrayOutputStream();
				try (java.io.FileInputStream fin = new java.io.FileInputStream(cfgFile)) {
					byte[] hbuf = new byte[65536];
					int hn;
					while ((hn = fin.read(hbuf)) > 0)
						haveBuf.write(hbuf, 0, hn);
				}
				String have = haveBuf.toString("UTF-8");
				int hn = have.indexOf('\n');
				String haveVersion = hn >= 0 ? have.substring(0, hn) : have;
				if (haveVersion.equals(wantedVersion)) {
					Log.i(TAG, "userconfig.cfg already at " + wantedVersion.trim());
					return;
				}
			}

			File cfgDir = new File(rodir, gamedir);
			if (!cfgDir.exists() && !cfgDir.mkdirs()) {
				Log.w(TAG, "could not create " + cfgDir.getAbsolutePath());
				return;
			}
			try (FileOutputStream out = new FileOutputStream(cfgFile)) {
				out.write(wanted.getBytes("UTF-8"));
			}
			Log.i(TAG, "Extracted " + wantedVersion.trim() + " to " + cfgFile.getAbsolutePath());
		} catch (Exception e) {
			Log.w(TAG, "Could not extract userconfig.cfg: " + e);
		}
	}
}
