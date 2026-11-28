#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44994623263
REPO=Kesurage/half-D
TMP=rel_v060
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
cp "$APK" "$TMP/half-D-v0.6.0.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

unzip -l "$TMP/half-D-v0.6.0.apk" | grep "lib/arm64-v8a/libref" > "$TMP/libs_check.txt" || true
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

notes = """## v0.6.0 — rolling the renderer back to the configuration that actually worked

You called it: "all comes down to the rendering problem". You were right, and the v0.5.4 report settled which direction is correct. Here's the honest engineering picture:

| build | visuals | fps | menus |
|---|---|---|---|
| v0.4.4 (gl4es) | correct — game shaders, lighting, terrain | 5-15 | working |
| v0.5.4 (native GLES3 experiment) | black ground, error models, artifacts | 0-4 | game + device crash |

**What went wrong:** v0.5.0 bet that running the game's desktop-GL renderer *natively* on your phone's GLES3 (with an automatic GLSL translator + call shim in between) would beat gl4es's translation overhead. Five rounds of capability fixes later, the renderer ran — but the translation cannot reproduce the game's rendering faithfully. Black terrain, broken characters, 0-4 fps, and menu crashes that reset the whole device are all symptoms of that one architectural mistake, not 10,000 separate bugs.

**What I verified against your reference:** I cloned yalnie/Diffusion-Android and diffed it against our tree. Its renderer code contains **zero** GLES-specific handling — the game expects a desktop GL 2.x context, and on Android it runs through **gl4es**. That's the architecture the game's author ships. Our only differences from it were my v0.5.x experiment stack, all of which is now verified inert on the gl4es path.

**The fix:** one architectural rollback — the engine now loads **gl4es first** (correct rendering, working menus) with the native path as automatic fallback. Everything from the working v0.4.4 era is still in place: menu crash fixes, texture fixes, the mobile quality preset (shadows/sun shafts/blur/FSR off by default, grass fades), and the perf instrumentation.

**What to expect:** the game should look right again and menus should not crash. Performance lands back at the v0.4.4 level — roughly 5-15 fps. That is the floor, not the ceiling: the v0.4.4 analysis showed the frame is CPU-bound in translation overhead, which is a real optimization target — but this time it gets attacked with data, not guesses.

**The ask:** play 3-5 minutes and send engine.log. The perf heartbeat (`perf: N fps | ...`) and the low-fps watchdog (`[watchdog] verdict: ...`) now run under gl4es too — they will name exactly which stage eats the frame time (scene polys vs postfx vs texture uploads vs draw-call count), and that log decides the next, data-driven optimization round.

**Note:** if you ever saved custom launcher arguments containing `-ref gles3compat`, remove them (or set `-ref gl4es`) — a saved pin would override the new default.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.6.0',
    'target_commitish': 'main',
    'name': 'v0.6.0 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.6.0.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
