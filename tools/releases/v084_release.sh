#!/bin/bash
# half-D v0.8.4 release: wait for CI on the pushed commit, download the
# APK artifact, verify renderer+game libs+config inside it, create the
# GitHub release and upload the APK.
#
# Token: RELEASE_TOKEN env var (falls back to the legacy .cred file).
set -euo pipefail
cd "$(dirname "$0")"
TOKEN="${RELEASE_TOKEN:-$(cat /home/z/my-project/.cred/token 2>/dev/null)}"
AUTH="Authorization: token $TOKEN"
REPO=Kesurage/half-D
SHA=$(git rev-parse HEAD)
TMP=rel_v084
rm -rf "$TMP" && mkdir -p "$TMP"

echo "waiting for CI on $SHA"
STATUS=in_progress
CONCL=""
RUNID=""
for i in $(seq 1 60); do
  JSON=$(curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs?head_sha=$SHA&per_page=1")
  STATUS=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['status'] if runs else 'missing')" 2>/dev/null || echo missing)
  CONCL=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['conclusion'] if runs and runs[0]['conclusion'] else '')" 2>/dev/null || echo '')
  RUNID=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['id'] if runs else '')" 2>/dev/null || echo '')
  echo "  [$i] status=$STATUS conclusion=$CONCL id=$RUNID"
  [ "$STATUS" = "completed" ] && break
  sleep 30
done
[ "$STATUS" = "completed" ] || { echo "CI did not complete in time"; exit 1; }
[ "$CONCL" = "success" ] || { echo "CI concluded $CONCL — no release"; exit 1; }

curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs/$RUNID/artifacts" > "$TMP/artifacts.json"
APK_ID=$(python3 - "$TMP/artifacts.json" << 'PYEOF'
import json, sys
for a in json.load(open(sys.argv[1]))['artifacts']:
    if a['name'] == 'half-D-engine-apk':
        print(a['id']); break
PYEOF
)
[ -n "$APK_ID" ] || { echo "APK artifact id missing"; exit 1; }

curl -sL -H "$AUTH" "https://api.github.com/repos/$REPO/actions/artifacts/$APK_ID/zip" -o "$TMP/apk.zip"
(cd "$TMP" && unzip -o -q apk.zip -d apk)
APK=$(find "$TMP/apk" -name "*.apk" | head -1)
[ -n "$APK" ] || { echo "no apk in artifact"; exit 1; }
cp "$APK" "$TMP/half-D-v0.8.4.apk"

unzip -l "$TMP/half-D-v0.8.4.apk" | grep -E "assets/cfg/userconfig.cfg|lib/arm64-v8a/libref_gl4es|lib/arm64-v8a/libclient_android_arm64" > "$TMP/libs_check.txt"
cat "$TMP/libs_check.txt"
grep -q "assets/cfg/userconfig.cfg" "$TMP/libs_check.txt" && grep -q "libref_gl4es.so" "$TMP/libs_check.txt" && grep -q "libclient_android_arm64.so" "$TMP/libs_check.txt" && echo "APK OK"

# confirm the shipped config actually carries the renderer flip
mkdir -p "$TMP/cfgcheck"
( cd "$TMP/cfgcheck" && unzip -o -q "../half-D-v0.8.4.apk" assets/cfg/userconfig.cfg )
grep -q 'gl_diffusion_renderer "0"' "$TMP/cfgcheck/assets/cfg/userconfig.cfg" && echo "config has gl_diffusion_renderer 0" || { echo "config MISSING the renderer flip"; exit 1; }

python3 - "$TMP" << 'PYEOF'
import json, os, sys, urllib.request
tmp = sys.argv[1]
token = os.environ['RELEASE_TOKEN']
def api(path, data=None):
    url = 'https://api.github.com' + path
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request(url, data=body, method='POST' if data else 'GET')
    req.add_header('Authorization', 'token ' + token)
    req.add_header('Accept', 'application/vnd.github+json')
    if data: req.add_header('Content-Type', 'application/json')
    return json.loads(urllib.request.urlopen(req).read())
notes = open('/home/z/my-project/download/v0.8.4-release-notes.md').read()
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.8.4',
    'target_commitish': 'main',
    'name': 'v0.8.4 — default renderer flipped to the engine path (gl_diffusion_renderer 0): fixes the v0.8.3 purple-lighting / glitched-model / 5-8 fps regression on Mali-G57/gl4es',
    'body': notes,
    'prerelease': False,
})
print('release:', rel['html_url'])
url = rel['upload_url'].replace('{?name,label}', '?name=half-D-v0.8.4.apk')
req = urllib.request.Request(url, data=open(os.path.join(tmp, 'half-D-v0.8.4.apk'), 'rb').read(), method='POST')
req.add_header('Authorization', 'token ' + token)
req.add_header('Content-Type', 'application/octet-stream')
r = urllib.request.urlopen(req)
print('uploaded:', json.loads(r.read())['size'], 'bytes')
PYEOF
