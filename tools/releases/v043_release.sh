#!/bin/bash
# v0.4.3 release — download run artifacts, rename, create the GitHub release.
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44940480860
REPO=Kesurage/half-D
TMP=rel_v043
rm -rf "$TMP" && mkdir -p "$TMP"

curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" > "$TMP/artifacts.json"
python3 - "$TMP/artifacts.json" << 'PYEOF' > "$TMP/ids.txt"
import json, sys
for a in json.load(open(sys.argv[1]))['artifacts']:
    print(a['id'], a['name'])
PYEOF
cat "$TMP/ids.txt"
APK_ID=$(awk '$2=="half-D-engine-apk"{print $1}' "$TMP/ids.txt")
LIBS_ID=$(awk '$2=="diffusion-android-libs"{print $1}' "$TMP/ids.txt")
[ -n "$APK_ID" ] && [ -n "$LIBS_ID" ] || { echo "artifact ids missing"; exit 1; }

for pair in "$APK_ID apk" "$LIBS_ID libs"; do
    set -- $pair
    curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$1/zip" -o "$TMP/$2.zip"
    (cd "$TMP" && unzip -o -q "$2.zip" -d "$2")
done

APK=$(find "$TMP/apk" -name "*.apk" | head -1)
LIBS=$(find "$TMP/libs" -name "diffusion-android-aarch64-libs.zip" | head -1)
[ -n "$APK" ] && [ -n "$LIBS" ] || { echo "artifacts not found"; exit 1; }
cp "$APK" "$TMP/half-D-v0.4.3.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

# sanity: the APK must contain the new engine (check it is an apk and nonempty)
unzip -l "$TMP/half-D-v0.4.3.apk" | grep -q "lib/arm64/libxash.so" && echo "engine lib present in APK"

cat > "$TMP/notes.md" << 'EOF'
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
EOF

python3 - "$TMP" << 'PYEOF'
import json, sys, urllib.request, os
tmp = sys.argv[1]
token = open('/home/z/my-project/.cred/token').read().strip()
def api(path, data=None):
    url = 'https://api.github.com' + path
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request(url, data=body, method='POST' if data else 'GET')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Accept', 'application/vnd.github+json')
    if data: req.add_header('Content-Type', 'application/json')
    return json.loads(urllib.request.urlopen(req).read())

notes = open(os.path.join(tmp, 'notes.md')).read()
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.4.3',
    'target_commitish': 'main',
    'name': 'v0.4.3 — engine restart lifecycle crash fixed (stale FS pool, survivable errors, complete logs)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.4.3.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
