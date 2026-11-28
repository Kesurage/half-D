#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44983074359
REPO=Kesurage/half-D
TMP=rel_v052
rm -rf "$TMP" && mkdir -p "$TMP"

curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" > "$TMP/artifacts.json"
python3 - "$TMP/artifacts.json" << 'PYEOF' > "$TMP/ids.txt"
import json, sys
for a in json.load(open(sys.argv[1]))['artifacts']:
    print(a['id'], a['name'])
PYEOF
APK_ID=$(awk '$2=="half-D-engine-apk"{print $1}' "$TMP/ids.txt")
LIBS_ID=$(awk '$2=="diffusion-android-libs"{print $1}' "$TMP/ids.txt")
[ -n "$APK_ID" ] && [ -n "$LIBS_ID" ] || { echo "artifact ids missing"; cat "$TMP/ids.txt"; exit 1; }

for pair in "$APK_ID apk" "$LIBS_ID libs"; do
    set -- $pair
    curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$1/zip" -o "$TMP/$2.zip"
    (cd "$TMP" && unzip -o -q "$2.zip" -d "$2")
done

APK=$(find "$TMP/apk" -name "*.apk" | head -1)
LIBS=$(find "$TMP/libs" -name "diffusion-android-aarch64-libs.zip" | head -1)
[ -n "$APK" ] && [ -n "$LIBS" ] || { echo "artifacts not found"; exit 1; }
cp "$APK" "$TMP/half-D-v0.5.2.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

unzip -l "$TMP/half-D-v0.5.2.apk" | grep "lib/arm64-v8a/libref" > "$TMP/libs_check.txt" || true
cat "$TMP/libs_check.txt"
grep -q "libref_gles3compat.so" "$TMP/libs_check.txt" && grep -q "libref_gl4es.so" "$TMP/libs_check.txt" && echo "RENDERERS OK"

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

notes = """## v0.5.2 — the multitexture veto, round two + a real engine-side overflow fix

Your second crash log told two stories at once.

**Story one — why the custom renderer was STILL disabled.** My v0.5.1 fix worked (the log printed `GL_VERSION: 3.2 (GLES)` — the context detection did its job), but the multitexture check failed one branch deeper: with `cvar_from_engine`, the game client asks the *engine* for its `gl_arb_multitexture` cvar instead of deciding itself. The engine's native GLES renderer never probes the desktop `GL_ARB_*` names, so that cvar is missing (or archived 0 after earlier failed probes) — and the check returned "not supported" without ever trying the function table, where everything resolves. Fixed: on GLES contexts the capability cvar value is ignored; the function table alone decides. Also added the ES3 fallback for the texture-unit limit query (`GL_MAX_TEXTURE_UNITS` is a fixed-function ES1 enum that native ES3 rejects — the engine already had this fallback, the client now does too).

**Story two — the actual crash.** With the custom renderer off, the engine's fallback renderer took over (it drew the whole world fine — a nice validation of the native ES3 path) and then died in `R_StudioBuildArrayFloatMesh` writing a studio mesh batch past its fixed 16384-vertex buffer. The game's high-poly models can expand past that mid-batch. All three mesh builders (normal/float/chrome) now truncate a batch at the buffer limit instead of writing past it.

**Net expectation for this test:** the game's own renderer should now initialize on the native path — menu, world, models, shaders. If anything still fails, the log will say exactly which capability, but this time every decision point on the ES path goes through the function table that was fully validated offline.

**Install:** APK over v0.5.1, extract the libs zip over the game folder.

**Send me the engine.log** after a few minutes of play — the `perf:` heartbeat lines decide the next optimization round. If it crashes again, the zip as usual; the stack will now be somewhere genuinely new, which is progress each time.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.5.2',
    'target_commitish': 'main',
    'name': 'v0.5.2 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.5.2.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
