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
	@JvmStatic
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
	@JvmStatic
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

	// half-D v0.8.8: the engine's LIVE log, wherever it really is. The
	// game-folder copy stopped being written after v0.8.1 on the field
	// device (the engine's fopen there fails silently), so a fixed path
	// returned a fossil five reports in a row. Candidates: the game folder
	// (the normal location), the app's private files dir (the engine's
	// v0.8.8 fallback, HALFD_LOGDIR) and the extracted gamelibs dir.
	@JvmStatic
	fun liveEngineLog(ctx: Context): File? {
		val name = CrashReports.ENGINELOG_NAME
		val candidates = listOf(
			File(Prefs.getGamePath(ctx), name),
			File(ctx.filesDir, name),
			File(File(ctx.filesDir, "gamelibs"), name),
		)
		return candidates.filter { it.isFile && it.length() > 0L }.maxByOrNull { it.lastModified() }
	}

	// Zip the given files (a directory is added recursively) plus a fresh
	// logcat snapshot, then open the system share sheet. Never throws.
	@JvmStatic
	fun shareFiles(ctx: Context, vararg files: File) {
		try {
			val zipDir = File(ctx.cacheDir, "sessions").apply { mkdirs() }
			val zip = File(zipDir, "halfD-logs-${stamp.format(Date())}.zip")
			java.util.zip.ZipOutputStream(java.io.BufferedOutputStream(java.io.FileOutputStream(zip))).use { zos ->
				for (f in files) {
					if (f.isFile) {
						addEntry(zos, f.name, f)
					} else {
						f.walkTopDown().filter { it.isFile }.forEach {
							addEntry(zos, "${f.name}/${it.relativeTo(f).path}", it)
						}
					}
				}
				val lc = File(zipDir, "logcat.txt")
				if (dumpLogcat(lc)) addEntry(zos, "logcat.txt", lc)
				lc.delete()
			}
			val authority = "${su.xash.engine.BuildConfig.APPLICATION_ID}.fileprovider"
			val uri = androidx.core.content.FileProvider.getUriForFile(ctx, authority, zip)
			val intent = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
				type = "application/zip"
				putExtra(android.content.Intent.EXTRA_SUBJECT, "half-D logs")
				putExtra(android.content.Intent.EXTRA_STREAM, uri)
				addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
			}
			ctx.startActivity(android.content.Intent.createChooser(intent, ctx.getString(su.xash.engine.R.string.crash_share)))
		} catch (e: Exception) {
			Log.w(TAG, "share failed: ${e.message}")
		}
	}

	private fun addEntry(zos: java.util.zip.ZipOutputStream, name: String, f: File) {
		zos.putNextEntry(java.util.zip.ZipEntry(name))
		f.inputStream().use { it.copyTo(zos) }
		zos.closeEntry()
	}
}
