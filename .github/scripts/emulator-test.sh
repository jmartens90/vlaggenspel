#!/usr/bin/env bash
# Usage: emulator-test.sh <apk> <output dir>
set -u
APK="$1"; OUT="$2"
mkdir -p "$OUT"
shot() {
  adb exec-out screencap -p > "$OUT/$1.png"
  echo "screenshot $1 | running pid: $(adb shell pidof "$PKG" | tr -d '\r') | focus: $(adb shell dumpsys window | grep -m1 mCurrentFocus | tr -d '\r')"
}
launch() { adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 > /dev/null 2>&1; }

adb install -r "$APK"
PKG=$(adb shell pm list packages | tr -d '\r' | sed 's/package://' | grep -i vlaggenspel | head -1)
echo "package: $PKG"
adb shell wm size | tee "$OUT/screen.txt"
SIZE=$(adb shell wm size | tr -d '\r' | grep -o '[0-9]*x[0-9]*' | tail -1)
W=${SIZE%x*}; H=${SIZE#*x}
adb logcat -c

launch; sleep 15; shot 01-start

# Draw an overshooting circle with one finger in the middle of the map.
CX=$((W * 2 / 5)); CY=$((H / 2)); R=$((H / 6))
python3 - "$CX" "$CY" "$R" > /tmp/draw.sh <<'PY'
import math, sys
cx, cy, r = map(int, sys.argv[1:])
pts = [(cx + r * math.cos(2 * math.pi * 1.15 * i / 36), cy + r * math.sin(2 * math.pi * 1.15 * i / 36)) for i in range(37)]
cmds = ["input motionevent DOWN %d %d" % pts[0]]
cmds += ["input motionevent MOVE %d %d" % p for p in pts[1:]]
cmds += ["input motionevent UP %d %d" % pts[-1]]
print("; ".join(cmds))
PY
adb shell "$(cat /tmp/draw.sh)"; sleep 3; shot 02-drawn

# Home button, wait, come back (this is where a white screen would show up).
adb shell input keyevent KEYCODE_HOME; sleep 5
launch; sleep 5; shot 03-resumed

# Turn the screen to portrait and back.
adb shell settings put system accelerometer_rotation 0
adb shell settings put system user_rotation 1; sleep 5; shot 04-rotated
adb shell settings put system user_rotation 0; sleep 5; shot 05-rotated-back

# Recent-apps switch and back.
adb shell input keyevent KEYCODE_APP_SWITCH; sleep 3
adb shell input keyevent KEYCODE_BACK; sleep 2
launch; sleep 5; shot 06-after-switch

# Kill the app completely and start it again: the autosaved world should come back.
adb shell am force-stop "$PKG"; sleep 2
launch; sleep 15; shot 07-restarted

# Back button (Android may close the app), then start again.
adb shell input keyevent KEYCODE_BACK; sleep 4
launch; sleep 10; shot 08-after-back

adb shell pidof "$PKG" > "$OUT/pid.txt" || echo "app not running" > "$OUT/pid.txt"
adb logcat -d > "$OUT/logcat.txt"
grep -iE "vlaggenspel|qt|FATAL|ANR|crash|AndroidRuntime" "$OUT/logcat.txt" | tail -300 > "$OUT/logcat-app.txt" || true
grep -iE "AndroidRuntime|FATAL|ANR in|qt\\.|libvlaggenspel|org.qtproject" "$OUT/logcat.txt" | tail -80
echo done
