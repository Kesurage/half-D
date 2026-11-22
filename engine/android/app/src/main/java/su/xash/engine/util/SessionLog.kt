package su.xash.engine.util

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// half-D v0.8.2: session log archive + native logcat capture.
//
// The engine writes the console output (Con_Printf, the "console logs")
// to <basedir>/engine.log when launched with -log, but that file is
// truncated by the next launch and the native/GPU-driver side of the
// story only ever lived in the logcat ring buffer, which is lost on
// every crash and device reboot. This object keeps both:
//
//  * archiveSession() — called at clean engine exit: copies the session's
//    engine.log and dumps the logcat buffer into a timestamped bundle
//    under the app's files dir (keep last MAX_SESSIONS).
//  * dumpLogcat() — called by the crash finalize path on the launch
//    AFTER a crash: the logcat buffer survives the app's death, so the
//    dump lands in the crash entry bundle next to crash.log/engine.log.
object SessionLog {
	private const val TAG = "SessionLog"
	private const val MAX_SESSIONS = 10
	private val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)

	fun sessionsRoot(ctx: Context): File = File(ctx.filesDir, "logs/sessions")

	// Dump the current logcat ring buffer (this app's own lines: the
	// engine's native stdout, SDL, and our launcher) to out.
	// Apps may read their own UID's log lines, so no permission needed.
	fun dumpLogcat(out: File): Boolean {
		return try {
			out.parentFile?.mkdirs()
			val proc = ProcessBuilder("logcat", "-d", "-v", "time")
				.redirectErrorStream(true)
				.start()
			val text = proc.inputStream.bufferedReader().use { it.readText() }
			proc.waitFor()
			out.writeText(text)
			Log.i(TAG, "logcat dump: ${out.length()} bytes -> ${out.path}")
			true
		} catch (e: Exception) {
			Log.w(TAG, "logcat dump failed: ${e.message}")
			false
		}
	}

	// Bundle a finished session: engine.log + logcat.txt under
	// files/logs/sessions/session-<timestamp>/. Safe to call from any
	// thread; never throws.
	fun archiveSession(ctx: Context, engineLog: File?) {
		try {
			val dir = File(sessionsRoot(ctx), "session-" + stamp.format(Date())).apply { mkdirs() }
			if (engineLog != null && engineLog.exists() && engineLog.length() > 0L) {
				engineLog.copyTo(File(dir, CrashReports.ENGINELOG_NAME), overwrite = true)
			}
			dumpLogcat(File(dir, CrashReports.LOGCAT_NAME))
			prune(ctx)
		} catch (e: Exception) {
			Log.w(TAG, "session archive failed: ${e.message}")
		}
	}

	private fun prune(ctx: Context) {
		val dirs = sessionsRoot(ctx).listFiles { f -> f.isDirectory }
			?.toMutableList() ?: return
		dirs.sortBy { it.name }
		while (dirs.size > MAX_SESSIONS) {
			val oldest = dirs.removeAt(0)
			if (!oldest.deleteRecursively())
				Log.w(TAG, "could not prune ${oldest.path}")
		}
	}
}
