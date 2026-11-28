#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=44981227799
REPO=Kesurage/half-D
TMP=rel_v051
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
cp "$APK" "$TMP/half-D-v0.5.1.apk"
cp "$LIBS" "$TMP/diffusion-android-aarch64-libs.zip"

unzip -l "$TMP/half-D-v0.5.1.apk" | grep "lib/arm64-v8a/libref" > "$TMP/libs_check.txt" || true
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

notes = """## v0.5.1 — crash fix: the renderer was fine, a 20-year-old extension-string check killed it

Your crash log actually carried good news before the crash: **the native GLES3 port worked** — `Loading renderer: gles3compat`, `GL_VERSION: 3.2 (GLES)`, the menu world loaded, map loading started, shaders compiled. Then:

```
Error: GL_ARB_multitexture is not supported. Custom renderer disabled.
```

One line after the context came up, the game disabled its own renderer. On desktop GL this check looks for the `GL_ARB_multitexture` **extension string**; native GLES3 has multitexture in its core and never prints that string. The check gave up before ever trying the functions — which were all present. With the renderer disabled, the world pointer stayed NULL and the first first-person view calculation hit it: segfault in the weapon-obstacle trace.

**Fixed in v0.5.1:**
- on GLES contexts the extension-string precheck is skipped — the function table alone decides (that's what v0.5.0 already made fully resolvable)
- the weapon-obstacle trace now guards against a missing world pointer, so even a future renderer-init failure degrades to "no weapon pullback" instead of a crash

**Install:** APK over v0.5.0, extract the libs zip over the game folder as before.

**What to look for now:** the game should reach actual gameplay on the native path. Send the engine.log after a few minutes of play — the perf heartbeat lines (`perf: N fps | ... stage ms: scene X`) will show what the native renderer really costs on your Helio G99, and that decides the next optimization round. `-ref gl4es` remains the escape hatch.
"""
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.5.1',
    'target_commitish': 'main',
    'name': 'v0.5.1 — crash fix: GL_ARB extension-string check was disabling the renderer on native GLES3 (multitexture is core there)',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
for name in ['half-D-v0.5.1.apk', 'diffusion-android-aarch64-libs.zip']:
    path = os.path.join(tmp, name)
    url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
    req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Content-Type', 'application/octet-stream')
    r = urllib.request.urlopen(req)
    print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF
echo DONE
