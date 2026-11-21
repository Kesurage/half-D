## v0.4.3 — this crash was an engine lifecycle bug, now survivable

**Straight answer: v0.4.2's crash was a bug in the engine itself — not the game, not your device, and not something you did.** Your crash dump showed:

```
Mem_FindPool: not allocated or double freed pool 5
Crash: signal 11 ... at 0x100 in FS_AddZip_Fullpath
```

### What happened
When the engine restarts inside the same Android process (which happens after an error dialog or activity relaunch), it resets its whole memory-pool table — but the filesystem module kept its old pool number ("5") from the previous run. That number now pointed past the end of the fresh table. On top of that, the engine's "fatal error" handler does not actually stop execution in this situation, so it returned a NULL pointer from the allocation and the zip-mounting code crashed writing through it. Two bugs stacked: a stale handle, and an error handler that doesn't halt.

The **original first error** that triggered the restart is still unknown — because the engine **truncated its own log file** on every restart, erasing the evidence. That's fixed too.

### What's fixed (all in the engine)
- The filesystem now **validates and self-heals** its pool handle across restarts.
- An invalid pool handle is a loud recoverable error, not a crash cascade.
- Every archive/directory mount checks its allocations — a failed mount is **skipped with an error naming the file**, never a crash.
- A restarted engine can actually shut down properly on a later fatal error.
- **`engine.log` now appends instead of truncating on in-process restarts** — if anything goes wrong again, the report will contain the ORIGINAL first error.

### Install
1. Install the APK over the old one.
2. Extract the libs zip over the game folder (same as before — it also carries the corrected rain texture if you ever installed v0.4.1).

### Test — same drill, and what to send
- Launch, let the shader load finish, try **Options → graphics options** (the still-unvalidated trigger from v0.4.1), then play for a few minutes.
- If the game shows an **error dialog** instead of crashing: note/screenshot its text — that's the real first error, finally visible.
- If anything crashes: send `engine.log` (or the crash zip) — with the new append mode it should now tell the whole story, including what happened before the restart.
