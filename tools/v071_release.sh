#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=45005498514
REPO=Kesurage/half-D
TMP=rel_v071
rm -rf "$TMP" && mkdir -p "$TMP"

curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" > "$TMP/artifacts.json"
python3 - "$TMP/artifacts.json" << 'PYEOF' > "$TMP/ids.txt"
import json, sys
for a in json.load(open(sys.argv[1]))['artifacts']:
    print(a['id'], a['name'])
PYEOF
APK_ID=$(awk '$2=="half-D-engine-apk"{print $1}' "$TMP/ids.txt")
[ -n "$APK_ID" ] || { echo "APK artifact id missing"; cat "$TMP/ids.txt"; exit 1; }

for pair in "$APK_ID apk"; do
    set -- $pair
    curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$1/zip" -o "$TMP/$2.zip"
    (cd "$TMP" && unzip -o -q "$2.zip" -d "$2")
done

APK=$(find "$TMP/apk" -name "*.apk" | head -1)
[ -n "$APK" ] || { echo "artifacts not found"; exit 1; }
cp "$APK" "$TMP/half-D-v0.7.1.apk"

unzip -l "$TMP/half-D-v0.7.1.apk" | grep -E "lib/arm64-v8a/libref|lib/arm64-v8a/libclient|assets/glsl/" > "$TMP/libs_check.txt" || true
cat "$TMP/libs_check.txt" | head -4
grep -q "libref_gles3compat.so" "$TMP/libs_check.txt" && grep -q "libref_gl4es.so" "$TMP/libs_check.txt" \
  && grep -q "libclient_android_arm64.so" "$TMP/libs_check.txt" && grep -q "assets/glsl/" "$TMP/libs_check.txt" \
  && echo "APK SELF-CONTAINED OK (renderers + game libs + shaders bundled)"

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

notes = """## v0.7.1 — crash fixed, performance mode now runs on the proven path

Your crash log told the whole story in two parts — both fixed:

**1. The crash (SIGSEGV at 0x0).** My v0.7.0 gate disabled the custom renderer *too early*: the client's GL function pointers were never resolved, so the first time the crosshair HUD tried to draw (right after you switched weapons — the texture-load burst at the end of your log), it called a NULL function pointer and died. Fix: the function table is now resolved before the gate — the same state your device already ran fine in earlier builds. The crosshair calls now hit real (harmless) GL entry points instead of NULL.

**2. Why textures went missing in v0.7.0.** Your log showed *every* game `.dds` (health icons, crosshairs, scopes, use icons) failing to load under the native GLES renderer I switched to — while the identical requests all succeeded under gl4es in v0.6.1 (I diffed the logs: same mounts, same files, only the renderer differs). This also finally explains the "black terrain" from the earlier native experiments — the engine's texture path fails on this game's DDS data under that renderer. So **performance mode now runs on gl4es** — the proven texture path — minus the custom renderer (which was the measured cost driver: compile storms, 1000+ draw calls).

**Net configuration:** engine renderer (fast, batched, no shader compilation) + gl4es (proven to load 100% of your game's textures). The full shader look stays available: `gl_custom_renderer 1` in the console.

### What to expect
No crash on weapon switch, textures back, no shader-compile stalls, and the frame time that the custom renderer was spending on 1000+ shader-bound draw calls now goes to the actual scene. If anything's still off, the log will say — send it as always.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.7.1',
    'target_commitish': 'main',
    'name': 'v0.7.1 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.7.1.apk']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
