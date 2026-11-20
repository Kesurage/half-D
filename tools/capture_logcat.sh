#!/usr/bin/env bash
# Capture engine logs from the device while play-testing.
# Usage: ./capture_logcat.sh [outfile]
set -eu
OUT="${1:-../logs/device-tests/logcat-$(date +%Y%m%d-%H%M%S).txt}"
mkdir -p "$(dirname "$OUT")"
echo "Filtering for xash3d/half-D tags. Ctrl-C to stop."
adb logcat -v time \
  Xash3D:V XashActivity:V libengine:V SDL:V OpenGLRenderer:V Mali:V \
  AndroidRuntime:E DEBUG:E '*:S' | tee "$OUT"
