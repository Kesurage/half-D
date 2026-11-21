#!/bin/bash
# v0.4.2 release — download run artifacts, rename, create the GitHub release.
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44936929355
REPO=Kesurage/half-D
TMP=rel_v042
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
cp "$APK" "$TMP/half-D-v0.4.2.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

# sanity: the FIXED raindrop must be inside the libs zip
unzip -p "$TMP/diffusion-android-aarch64-libs.zip" diffusion/gfx/particles/raindrop.dds | python3 -c "
import sys, struct
d = sys.stdin.buffer.read()
flags = struct.unpack('<I', d[8:12])[0]
linear = struct.unpack('<I', d[20:24])[0]
assert flags & 0x80000 and not (flags & 0x8), 'wrong DDS flags'
assert linear == 32*128*4 == len(d)-128, 'wrong linear size'
print('raindrop.dds verified: LINEARSIZE flag, total', linear, 'bytes — exact match')
"

cat > "$TMP/notes.md" << 'EOF'
## v0.4.2 — the v0.4.1 crash was my own bug, found and fixed

**Straight answer first: v0.4.1's crash was caused by my own rain-texture file, not your device and not the options menu.** Your crash dump contained the smoking gun:

```
Mem_FreeBlockBig: trashed header sentinel 2
(alloc at engine/common/imagelib/img_dds.c:420 ...)
```

### What happened
The replacement rain texture I shipped in v0.4.1 (to fix the BC7 texture your GPU can't decode) had a malformed DDS header: it declared the **per-row pitch (128 bytes)** where the loader expects the **total buffer size (16384 bytes)**. The engine therefore allocated a 128-byte buffer for a 16 KB texture, and the texture upload wrote straight through it — **heap corruption**. The actual crash backtraces pointed into Android system libraries (text rendering in `libhwui`/`libft2`, `RefBase::incStrong`) because that's where the corrupted heap happened to detonate — 5 seconds after launch, before the menu even appeared.

### What's fixed
- **The DDS file**: now declares the correct total size; the loader's own size verification passes exactly. I verified the fix by replicating the loader's allocation logic byte-for-byte against the new header.
- **The loader itself is hardened**: an uncompressed DDS that declares a per-row pitch is now sized as pitch × height, so no file of this shape can corrupt the heap again — including the broken v0.4.1 one, if it were still on disk.

### Everything from v0.4.1 is still in (it never got to run)
The crash happened during texture init, before any of v0.4.1's actual fixes executed: the hard gate on all 11 screen-capture effects (the options-menu crash fix), the shader failure negative-cache and logging, the precompile filter (365 impossible shadow permutations skipped), the load-phase timing, and the perf heartbeat with draw-call counts.

**Install:** APK **and** extract the new libs zip over the game folder (it contains the corrected raindrop.dds — make sure it overwrites the v0.4.1 one).

**Test:** same as last time — let the shader load finish, open Options and click graphics options (that's the still-unvalidated crash trigger), then send `engine.log`.
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
    'tag_name': 'v0.4.2',
    'target_commitish': 'main',
    'name': 'v0.4.2 — heap corruption from my v0.4.1 raindrop.dds fixed (loader hardened)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.4.2.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
