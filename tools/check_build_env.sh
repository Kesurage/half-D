#!/usr/bin/env bash
# half-D build environment check (Phase 1)
# Verifies the toolchain needed to fork & build xash3d-fwgs for Android.
set -u
fail=0
need() { # $1=cmd $2=what for
  if command -v "$1" >/dev/null 2>&1; then
    printf '  [OK]   %s (%s)\n' "$1" "$2"
  else
    printf '  [MISS] %s (%s)\n' "$1" "$2"; fail=1
  fi
}
ver() { command -v "$1" >/dev/null 2>&1 && "$1" --version 2>&1 | head -n1; }

echo "== half-D build environment check =="
need git        "cloning the engine fork"
need python3    "waf build system (python-based)"
need java       "JDK 17 for Gradle"
need javac      "compiling the Android app"
need gradle     "or use the repo's gradle wrapper"
need sdkmanager "Android SDK packages"
need ndk-build  "Android NDK (or set ANDROID_NDK_HOME)"
need adb        "deploying to the Hot 50 4G"
need aapt2      "APK packaging"

[ -n "${ANDROID_HOME:-}" ]    && echo "  [OK]   ANDROID_HOME=$ANDROID_HOME"    || { echo "  [MISS] ANDROID_HOME"; fail=1; }
[ -n "${ANDROID_NDK_HOME:-}" ] && echo "  [OK]   ANDROID_NDK_HOME=$ANDROID_NDK_HOME" || echo "  [WARN] ANDROID_NDK_HOME not set"
[ -n "${JAVA_HOME:-}" ]       && echo "  [OK]   JAVA_HOME=$JAVA_HOME"          || echo "  [WARN] JAVA_HOME not set"

echo "-- versions --"
for c in git python3 java; do [ $fail -eq 0 ] && ver "$c"; done

if [ $fail -eq 1 ]; then
  echo "RESULT: incomplete — install missing tools before Phase 1."
  exit 1
fi
echo "RESULT: environment looks ready."
