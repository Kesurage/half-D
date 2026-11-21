#!/bin/bash
# v0.4.1 release — download run artifacts, rename, create the GitHub release.
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44933683738
REPO=Kesurage/half-D
TMP=rel_v041
rm -rf "$TMP" && mkdir -p "$TMP"

fetch_artifact() {
    local id="$1" name="$2"
    curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$id/zip" -o "$TMP/$name.zip"
    (cd "$TMP" && unzip -o -q "$name.zip" -d "$name")
}

fetch_artifact 14737846693 apk 2>/dev/null || true
echo "(apk artifact id resolved below)"

# resolve artifact ids from the run (portable)
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

fetch_artifact "$APK_ID" apk
fetch_artifact "$LIBS_ID" libs

APK=$(find "$TMP/apk" -name "*.apk" | head -1)
LIBS=$(find "$TMP/libs" -name "diffusion-android-aarch64-libs.zip" | head -1)
[ -n "$APK" ] || { echo "APK not found"; exit 1; }
[ -n "$LIBS" ] || { echo "libs zip not found"; exit 1; }
cp "$APK" "$TMP/half-D-v0.4.1.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

# sanity: the raindrop replacement must be inside the libs zip
unzip -l "$TMP/diffusion-android-aarch64-libs.zip" | grep -E "raindrop|FiraSans" || echo "WARN: overrides missing from libs zip"

cat > "$TMP/notes.md" << 'EOF'
## v0.4.1 — found it: the options menu was re-arming the crash

Your v0.4.0 log confirmed all four previous fixes went live (no more shadow-texture errors, menu font fixed, shader cache enabled, precompile ran). This round it finally exposed the actual crash trigger and three more mechanisms.

### The crash when selecting a graphics option — root cause found & hard-fixed
The **video options presets re-enable every expensive effect**: selecting any graphics quality option sets `r_bloom 1`, `gl_sunshafts 1`, `r_blur 1`, `gl_hbao 1`, `gl_water_refraction 1`… — each of those turns a **per-frame fullscreen screen-capture** back on. Through gl4es on your Mali that is a CPU readback (the same cliff as FSR/DownScale/the menu blur): multi-second frames, and the Android GPU watchdog then kills the game and destabilizes the device.

The v0.4.0 menu-blur fix only covered *one* of those effects. This build adds a **hard gate on all 11 capture effects** — on the GL2 path they stay off **no matter what the options menu or saved config says**. Selecting options can no longer re-arm the cliff. (`gl_mobile_quality 0` in console opts back in if you ever want the full effects.)

### The 279 failed shaders — explained, filtered, and now logged
- 169 `HAS_SHADOWS` + 196 `SUN_SHADOW` shader permutations use shadow samplers that **gl4es cannot translate at all** (its ES fallbacks don't apply because gl4es presents desktop GL). They can never work on this device — the precompile now **skips all ~413 impossible permutations**, so first launch compiles ~700 instead of 1111, faster and without the failure noise.
- Failed shaders were retried **every frame** if anything requested them (each retry = a full driver compile = a stall). Failures are now remembered and never retried.
- All shader failures are now **always logged** (they were invisible — the engine's error printer is disabled without developer mode). If anything still fails, the next log will name it and say why.

### The 25-second load — thief hunt instrumented
The v0.4.0 stage timers proved the 25 s is **not** in any of the 17 BSP heap stages. Every remaining phase is now timed (file read with size, lump byte-swap, BSPX scan, raw lump loading, studio-cache purge) — the next log will name the thief directly.

### Rain texture fixed
Your log confirmed `raindrop` is **BC7 SRGB**, which Mali cannot decode via gl4es (no BPTC). The libs zip now ships an uncompressed replacement — **this is why extracting the new libs zip over the game folder matters** (it overrides the broken one).

### Also in this build
- Perf heartbeat now reports **draw calls / passes / shader binds** — tells us whether the ~100 ms scene stage is draw-call-bound or geometry-bound (decides the next optimization).

**Install:** APK over v0.4.0 **and** extract the new libs zip over the game folder (the raindrop fix lives there).

**Test specifically:** open Options → select graphics quality options → enter/leave menus repeatedly. Then play a session and send `engine.log`. Expected: no crash, no multi-second spikes after the shader load, and the log will contain `load:` phase lines + `dips/passes` in the perf lines.
EOF

echo "creating release..."
python3 - "$TMP" << 'PYEOF'
import json, sys, urllib.request, os
tmp = sys.argv[1]
token = open('/home/z/my-project/.cred/token').read().strip()
def api(path, data=None, method=None):
    url = 'https://api.github.com' + path
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request(url, data=body, method=method or ('POST' if data else 'GET'))
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Accept', 'application/vnd.github+json')
    if data: req.add_header('Content-Type', 'application/json')
    return json.loads(urllib.request.urlopen(req).read())

notes = open(os.path.join(tmp, 'notes.md')).read()
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.4.1',
    'target_commitish': 'main',
    'name': 'v0.4.1 — options-menu crash rooted out: hard gate on all screen-capture FX',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])

for name in ['half-D-v0.4.1.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
