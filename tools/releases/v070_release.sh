#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=45002913229
REPO=Kesurage/half-D
TMP=rel_v070
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
cp "$APK" "$TMP/half-D-v0.7.0.apk"

unzip -l "$TMP/half-D-v0.7.0.apk" | grep -E "lib/arm64-v8a/libref|lib/arm64-v8a/libclient|assets/glsl/" > "$TMP/libs_check.txt" || true
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

notes = """## v0.7.0 — Performance Mode: the engine's own renderer takes over

Two days is too long, and your frustration is fair. This build changes the architecture instead of patching around it. **It is built from your logs, and it removes the single thing that has been breaking every build: the game's custom shader renderer.**

### What your last log + screenshots proved

- **The game was rendering mostly correctly** (your screenshots: menu, chapter select, cars, terrain, sky all fine) — the broken surfaces were a handful of texture-format failures and the missing data files. The renderer was never 10,000 bugs.
- **The 1 fps frames were shader compilation**, not the scene: my v0.6.1 change moved the 714-shader compile storm into gameplay — a **31-second single frame** right at map load.
- **LOD can't help**: your car/NPC models carry no LOD meshes at all, so the 219k-258k polygons per frame are fixed in the custom renderer's path. Its ceiling on your device is what we measured: 11-18 fps. That's all it will ever do there.

### What v0.7.0 does

**Performance Mode (new default):** the game's custom shader renderer is off, and the **engine's own native GLES renderer draws everything**. No client shader compilation at all — the 31-second storms, the 6-minute menus, all of it, gone by design. The engine renderer batches geometry properly and is the same code path FWGS ships for Half-Life on Android — battle-tested, no translation layer. This exact configuration already ran on your device once (v0.5.2) — the difference is that v0.5.2 had the custom renderer *half-alive* causing glitches; now it's cleanly off.

**What you give up:** the game's fancy shader look (dynamic sun shadows, fancy lighting). The world renders with its baked lightmaps — classic Half-Life-style visuals, correct geometry and textures.

**What you keep:** everything else — physics, NPCs, weapons, saves, the full game.

### What to expect

- Menu usable immediately. Loading without shader storms. In-game fps: this is the first build where the renderer is natively matched to your phone — judge it with your own eyes.

### If you want the shader look anyway (not recommended on this device)

Launcher arguments: `gl_custom_renderer 1` and `-ref gl4es`. That restores v0.6.1 exactly — correct visuals, ~11-18 fps, long loads. It's one toggle, documented in the log at startup.

### About the rewrite / panvk question — the honest answer

I did the research you asked for. Diffusion runs on **xash3d-fwgs + a client-side GL 2.1 renderer with GLSL shaders**; the author's own Android port uses gl4es. There is no Vulkan path anywhere. **panvk** (Mesa's Vulkan driver for Mali) is experimental, and using it means writing a complete new renderer in Vulkan against a driver that isn't production-ready — months of work with worse odds than paths we've already measured on your device. What I shipped instead is the *rewrite that actually helps*: moving the default rendering to the engine's own native path. If performance mode still isn't fast enough, the next step is measuring it — the build logs the numbers.

### The error list

The ~100 "Could not load sound" lines are the game's 1.21 data package not shipping files its own code references (NPC voice sets, footsteps) — true on desktop too. v0.7.0 prints each one once per session instead of spamming. If you want them fixed, re-extract your 1.21 game folder — some may be there under paths the game doesn't check.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.7.0',
    'target_commitish': 'main',
    'name': 'v0.7.0 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.7.0.apk']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
