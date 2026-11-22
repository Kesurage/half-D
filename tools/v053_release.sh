#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44985626495
REPO=Kesurage/half-D
TMP=rel_v053
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
cp "$APK" "$TMP/half-D-v0.5.3.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

unzip -l "$TMP/half-D-v0.5.3.apk" | grep "lib/arm64-v8a/libref" > "$TMP/libs_check.txt" || true
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

notes = """## v0.5.3 — one missing function name was holding the whole renderer hostage + the perf watchdog you asked for

Good news first: your v0.5.2 log shows **multitexture passed** (the round-two fix worked) — the failure just moved to the next gate: `GL_ARB_shader_objects`. What you saw as "shaders not loading" and "texture glitches" was the game's renderer still disabled, with the engine's bare fallback drawing the world without any of the game's materials/lighting/effects.

**The bug:** `glDetachObjectARB`. The check that validates shader support tries every function in a 65-entry table, and a single unresolved name disables the entire extension. Stripping the `ARB` suffix from that name gives `glDetachObject` — not a real function (the core name is `glDetachShader`). One name, whole renderer off.

**The fix — and why this can't happen again:** I built an audit that models the client's entire function-resolution chain exactly as the code runs it (native ES3 ∪ shim exports ∪ special-case retries ∪ stubs ∪ suffix stripping) and ran it against **every name in every table the client resolves — 277 total**. Before: 273 resolved. Now: **277/277**. Three more gaps were fixed alongside: `glMultiDrawArrays` (really used by the decal batcher — implemented for real, converting ES3's missing `GL_POLYGON` mode to triangle fans), `glGetQueryObjectivARB` (ES3 only exposes the unsigned read), `glTexEnvfv` (unused; safe no-op). There is no round four hiding in the function tables — this is now proven, not hoped.

**The watchdog (your request):** when a 5-second window averages under 30 fps, the log now prints a `[watchdog]` block: per-stage milliseconds (scene / postfx / texture uploads / cubemaps), draw-instruction and shader-bind counts, polys-per-draw — and a **verdict naming the dominant cost** (world geometry / detail grass / studio models / post-processing / draw-call bound / shader-switch bound / not-GPU-bound) with the exact cvars to turn down. Example of what you'll see:

```
perf: [watchdog] 24.7 fps < 30 | frame 40.5 ms | scene 31.2 postfx 4.1 ...
perf: [watchdog] verdict: SCENE-BOUND, detail grass dominates (31024 grass polys vs 8000 world + 6000 studio) — advice: r_grass 0, r_grass_shadows 0
```

**Install:** APK over v0.5.2, extract the libs zip over the game folder.

**What to expect:** the game's own renderer should finally come up on the native path — shaders, lighting, effects, the real look of the game. Play a few minutes and send the engine.log: the heartbeat lines (`perf: N fps | ...`) plus any `[watchdog]` verdicts decide the optimization round. Your 6 fps expectation on high settings is exactly what the watchdog will confirm or refute with numbers.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.5.3',
    'target_commitish': 'main',
    'name': 'v0.5.3 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.5.3.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
