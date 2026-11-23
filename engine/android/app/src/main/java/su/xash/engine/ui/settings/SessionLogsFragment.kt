package su.xash.engine.ui.settings

import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.core.content.FileProvider
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.util.CrashReports
import su.xash.engine.util.Prefs
import su.xash.engine.util.SessionLog
import su.xash.engine.util.monospaceTextView
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

// half-D v0.8.7: session logs with one-tap share. Field debugging was
// flying blind because the manually-extracted engine.log kept being a
// stale file from an old session (the game folder copy is only rewritten
// at the next engine start, and crashed sessions are never archived).
// This screen always shows the LIVE engine.log first — present in the
// game folder even right after a crash, before the next launch overwrites
// it — plus the cleanly-exited session archives, each shareable as a zip
// (archived sessions include their logcat; the live entry takes a fresh
// logcat dump so native crash traces survive too).
class SessionLogsFragment : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceScreen = preferenceManager.createPreferenceScreen(requireContext())
		populate()
	}

	override fun onResume() {
		super.onResume()
		populate()
	}

	private fun populate() {
		val ctx = requireContext()
		preferenceScreen.removeAll()
		val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)

		// The live engine.log is the one that matters: it is the most
		// recent session, crash or clean, until the next launch.
		val live = File(Prefs.getGamePath(ctx), "engine.log")
		if (!live.isFile || live.length() == 0L) {
			preferenceScreen.addPreference(Preference(ctx).apply {
				setTitle(R.string.session_logs_empty)
				isSelectable = false
			})
			return
		}

		preferenceScreen.addPreference(Preference(ctx).apply {
			title = getString(R.string.session_log_live)
			summary = fmt.format(Date(live.lastModified()))
			setOnPreferenceClickListener {
				showShareDialog(live, null)
				true
			}
		})

		val dirs = SessionLog.sessionsRoot(ctx).listFiles()
			?.filter { it.isDirectory }
			?.sortedByDescending { it.lastModified() }
			?: emptyList()

		dirs.forEach { dir ->
			preferenceScreen.addPreference(Preference(ctx).apply {
				title = fmt.format(Date(dir.lastModified()))
				summary = dir.name
				setOnPreferenceClickListener {
					showShareDialog(File(dir, CrashReports.ENGINELOG_NAME), dir)
					true
				}
			})
		}
	}

	private fun showShareDialog(log: File, sessionDir: File?) {
		val ctx = requireContext()
		val tail = runCatching {
			val bytes = log.readBytes()
			String(bytes.copyOfRange(maxOf(0, bytes.size - 32768), bytes.size))
		}.getOrDefault("(could not read ${log.name})")

		AlertDialog.Builder(ctx)
			.setTitle(sessionDir?.name ?: log.name)
			.setView(monospaceTextView(ctx, tail))
			.setPositiveButton(R.string.crash_share) { _, _ -> share(ctx, log, sessionDir) }
			.apply {
				if (sessionDir != null) {
					setNeutralButton(R.string.crash_log_delete) { _, _ ->
						sessionDir.deleteRecursively()
						populate()
					}
				}
			}
			.show()
	}

	private fun share(ctx: Context, log: File, sessionDir: File?) {
		runCatching {
			val zipDir = File(ctx.cacheDir, "sessions").apply { mkdirs() }
			val zip = File(zipDir, "session-${System.currentTimeMillis()}.zip")
			ZipOutputStream(BufferedOutputStream(FileOutputStream(zip))).use { zos ->
				if (sessionDir != null) {
					sessionDir.walkTopDown().filter { it.isFile }.forEach { f ->
						zos.putNextEntry(ZipEntry("${sessionDir.name}/${f.relativeTo(sessionDir).path}"))
						f.inputStream().use { it.copyTo(zos) }
						zos.closeEntry()
					}
				} else {
					zos.putNextEntry(ZipEntry("engine.log"))
					log.inputStream().use { it.copyTo(zos) }
					zos.closeEntry()

					// native crash traces (tombstone, ANR) never reach
					// engine.log — grab a logcat snapshot while we're here
					val lc = File(zipDir, "logcat.txt")
					if (SessionLog.dumpLogcat(lc)) {
						zos.putNextEntry(ZipEntry("logcat.txt"))
						lc.inputStream().use { it.copyTo(zos) }
						zos.closeEntry()
					}
					lc.delete()
				}
			}

			val authority = "${BuildConfig.APPLICATION_ID}.fileprovider"
			val uri = FileProvider.getUriForFile(ctx, authority, zip)
			val intent = Intent(Intent.ACTION_SEND).apply {
				type = "application/zip"
				putExtra(Intent.EXTRA_SUBJECT, "half-D session logs")
				putExtra(Intent.EXTRA_STREAM, uri)
				addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
			}
			ctx.startActivity(Intent.createChooser(intent, ctx.getString(R.string.session_logs)))
		}
	}
}
