#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TOKEN=$(cat /home/z/my-project/.cred/token)
AUTH="Authorization: token $TOKEN"
REPO=Kesurage/half-D
TMP=rel_v080
rm -rf "$TMP" && mkdir -p "$TMP"

# find the CI run for the pushed commit and wait for completion
SHA=$(git rev-parse HEAD)
echo "waiting for CI on $SHA"
STATUS=in_progress
for i in $(seq 1 60); do
  JSON=$(curl -s -H "$AUTH" "https://api.github.com/repos/$REPO/actions/runs?head_sha=$SHA&per_page=1")
  STATUS=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['status'] if runs else 'missing')" 2>/dev/null || echo missing)
  CONCL=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['conclusion'] if runs else '')" 2>/dev/null || echo '')
  RUNID=$(echo "$JSON" | python3 -c "import json,sys; runs=json.load(sys.stdin)['workflow_runs']; print(runs[0]['id'] if runs else '')" 2>/dev/null || echo '')
  echo "  status=$STATUS conclusion=$CONCL id=$RUNID"
  [ "$STATUS" = "completed" ] && break
  [ "$STATUS" = "missing" ] && { echo "no run found yet"; }
  sleep 30
done
[ "$STATUS" = "completed" ] || { echo "CI did not complete"; exit 1; }
[ "$CONCL" = "success" ] || { echo "CI concluded $CONCL"; exit 1; }

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
cp "$APK" "$TMP/half-D-v0.8.0.apk"

unzip -l "$TMP/half-D-v0.8.0.apk" | grep -E "lib/arm64-v8a/libref_gl4es|lib/arm64-v8a/libclient_android_arm64" > "$TMP/libs_check.txt"
cat "$TMP/libs_check.txt"
grep -q "libref_gl4es.so" "$TMP/libs_check.txt" && grep -q "libclient_android_arm64.so" "$TMP/libs_check.txt" && echo "APK OK"

python3 - "$TMP" << 'PYEOF'
import json, sys, os, urllib.request
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
notes = open('/home/z/my-project/download/v0.8.0-release-notes.md').read()
rel = api('/repos/Kesurage/half-D/releases', {
    'tag_name': 'v0.8.0',
    'target_commitish': 'main',
    'name': 'v0.8.0 — freeze-crash fixed (budgeted shader compilation), heavy scene 4 passes to 1',
    'body': notes,
    'prerelease': False,
})
print('release:', rel['html_url'])
url = rel['upload_url'].replace('{?name,label}', '?name=half-D-v0.8.0.apk')
req = urllib.request.Request(url, data=open(os.path.join(tmp, 'half-D-v0.8.0.apk'), 'rb').read(), method='POST')
req.add_header('Authorization', 'token ' + token)
req.add_header('Content-Type', 'application/octet-stream')
r = urllib.request.urlopen(req)
print('uploaded:', json.loads(r.read())['size'], 'bytes')
PYEOF

cp "$TMP/half-D-v0.8.0.apk" /home/z/my-project/download/half-D-v0.8.0.apk
echo DONE
