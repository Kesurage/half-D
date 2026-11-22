#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
RUN_ID=45012574162
REPO=Kesurage/half-D
TMP=rel_v073
rm -rf "$TMP" && mkdir -p "$TMP"

curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" > "$TMP/artifacts.json"
python3 - "$TMP/artifacts.json" << 'PYEOF' > "$TMP/ids.txt"
import json, sys
for a in json.load(open(sys.argv[1]))['artifacts']:
    print(a['id'], a['name'])
PYEOF
APK_ID=$(awk '$2=="half-D-engine-apk"{print $1}' "$TMP/ids.txt")
[ -n "$APK_ID" ] || { echo "APK artifact id missing"; cat "$TMP/ids.txt"; exit 1; }

curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$APK_ID/zip" -o "$TMP/apk.zip"
(cd "$TMP" && unzip -o -q "apk.zip" -d apk)

APK=$(find "$TMP/apk" -name "*.apk" | head -1)
[ -n "$APK" ] || { echo "artifacts not found"; exit 1; }
cp "$APK" "$TMP/half-D-v0.7.3.apk"

# sanity: renderers + game libs + shaders + the v0.7.2 skybox all bundled
unzip -l "$TMP/half-D-v0.7.3.apk" | grep -E "lib/arm64-v8a/libref|lib/arm64-v8a/libclient|assets/glsl/|gfx/env/desert" > "$TMP/libs_check.txt" || true
head -6 "$TMP/libs_check.txt"
grep -q "libref_gles3compat.so" "$TMP/libs_check.txt" && grep -q "libref_gl4es.so" "$TMP/libs_check.txt" \
  && grep -q "libclient_android_arm64.so" "$TMP/libs_check.txt" && grep -q "assets/glsl/" "$TMP/libs_check.txt" \
  && grep -q "gfx/env/desert" "$TMP/libs_check.txt" \
  && echo "APK SELF-CONTAINED OK (renderers + game libs + shaders + skybox bundled)"

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

notes = open('/home/z/my-project/download/v0.7.3-release-notes.md').read()
open(os.path.join(tmp, 'notes.md'), 'w').write(notes)
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.7.3',
    'target_commitish': 'main',
    'name': 'v0.7.3 — the full renderer is back: materials, lighting, effects and correct props; the stuck loading-shaders bar is fixed',
    'body': notes,
    'prerelease': False,
})
print('release id:', rel['id'], rel['html_url'])
name = 'half-D-v0.7.3.apk'
path = os.path.join(tmp, name)
url = rel['upload_url'].replace('{?name,label}', '?name=' + name)
req = urllib.request.Request(url, data=open(path, 'rb').read(), method='POST')
req.add_header('Authorization', 'token ' + token)
req.add_header('Content-Type', 'application/octet-stream')
r = urllib.request.urlopen(req)
print('uploaded:', name, json.loads(r.read())['size'], 'bytes')
PYEOF

cp "$TMP/half-D-v0.7.3.apk" /home/z/my-project/download/half-D-v0.7.3.apk
echo DONE
