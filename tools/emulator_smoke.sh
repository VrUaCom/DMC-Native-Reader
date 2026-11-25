#!/usr/bin/env bash
# The check an APK passes before it goes to a phone: on a running Android
# emulator it installs the APK, starts the app, opens each test file the way
# a file manager does, and fails if the app is gone or the log shows a crash.
# Then it checks the crash notes themselves: a Java exception and a native
# fault while a file opens must leave a report, and the file must open as its
# description the next time.
#
#   tools/emulator_smoke.sh <app.apk> <dir with test files> [adb serial]
#
# The APK must match the emulator's CPU: build it with
#   gradle :app:assembleDebug -PdmcAbi=x86_64
# Test files are game files; they stay outside the repository.
set -uo pipefail

APK="${1:?usage: emulator_smoke.sh <app.apk> <dir with test files> [adb serial]}"
FILES="${2:?usage: emulator_smoke.sh <app.apk> <dir with test files> [adb serial]}"
SERIAL="${3:-emulator-5554}"
ADB="${ADB:-adb} -s ${SERIAL}"
PACKAGE=com.dmcrengine.nativereader
OPEN_ACTIVITY="${PACKAGE}/com.dmcrengine.nativeviewer.DmcOpenActivity"
MAIN_ACTIVITY="${PACKAGE}/com.dmcrengine.nativeviewer.MainActivity"
SETTLE="${SMOKE_SETTLE_SECONDS:-30}"
CRASH_PATTERN='FATAL EXCEPTION|JNI DETECTED ERROR|Fatal signal|Abort message|UnsatisfiedLinkError'
failures=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL: $*"; failures=$((failures + 1)); }
alive() { [ -n "$($ADB shell pidof "$PACKAGE" 2>/dev/null | tr -d '\r')" ]; }
crash_lines() { $ADB logcat -d 2>/dev/null | grep -E "$CRASH_PATTERN" | head -5; }
mime_of() {
    case "${1,,}" in
        *.mod) echo application/vnd.dmc.mod ;;
        *.scm) echo application/vnd.dmc.scm ;;
        *.pac) echo application/vnd.dmc.pac ;;
        *.ptx) echo application/vnd.dmc.ptx ;;
        *.mot) echo application/vnd.dmc.mot ;;
        *.dds) echo image/vnd-ms.dds ;;
        *) echo application/octet-stream ;;
    esac
}
# Opens a file of the app's own storage through the open-with activity.
open_file() {  # name [extra key value]
    local name="$1"; shift
    $ADB shell am start -W -a android.intent.action.VIEW -t "$(mime_of "$name")" \
        -d "file:///data/user/0/${PACKAGE}/files/smoke/${name}" "$@" -n "$OPEN_ACTIVITY" >/dev/null 2>&1
}
closed_on() { $ADB shell "run-as $PACKAGE cat shared_prefs/crash.xml" 2>/dev/null | grep -c 'name="closedOn"'; }

say "== install $(basename "$APK")"
$ADB install -r "$APK" >/dev/null 2>&1 || { say "FAIL: install"; exit 1; }
$ADB shell "run-as $PACKAGE mkdir -p files/smoke"
names=()
for path in "$FILES"/*; do
    [ -f "$path" ] || continue
    name="$(basename "$path")"
    $ADB push "$path" "/data/local/tmp/$name" >/dev/null 2>&1
    $ADB shell chmod 644 "/data/local/tmp/$name"
    $ADB shell "run-as $PACKAGE cp /data/local/tmp/$name files/smoke/$name"
    $ADB shell rm -f "/data/local/tmp/$name"
    names+=("$name")
done
[ "${#names[@]}" -gt 0 ] || { say "FAIL: no test files in $FILES"; exit 1; }

say "== start from the icon"
$ADB shell am force-stop "$PACKAGE"; $ADB logcat -c
$ADB shell am start -W -n "$MAIN_ACTIVITY" >/dev/null 2>&1
sleep "$SETTLE"
if alive && [ -z "$(crash_lines)" ]; then say "ok   start"; else fail "start"; crash_lines; fi

for name in "${names[@]}"; do
    $ADB shell am force-stop "$PACKAGE"; $ADB logcat -c
    open_file "$name"
    sleep "$SETTLE"
    if alive && [ -z "$(crash_lines)" ]; then say "ok   open $name"; else fail "open $name"; crash_lines; fi
done

first="${names[0]}"
for kind in java native; do
    say "== crash notes: $kind"
    $ADB shell am force-stop "$PACKAGE"; $ADB logcat -c
    open_file "$first" --es dmc.test.crash "$kind"
    sleep 12
    if alive; then fail "$kind test crash did not stop the app"; fi
    note="crash_${kind}.txt"
    if [ -n "$($ADB shell "run-as $PACKAGE cat files/$note" 2>/dev/null | head -1)" ]; then
        say "ok   $note written"
    else
        fail "$note missing"
    fi
    # The next start reads the notes, and the file opens as a description.
    $ADB logcat -c
    open_file "$first"
    sleep "$SETTLE"
    if alive && [ -z "$(crash_lines)" ]; then say "ok   restart after $kind crash"; else fail "restart after $kind crash"; crash_lines; fi
    if [ "$(closed_on)" = "1" ]; then say "ok   $first remembered as the file it closed on"; else fail "closed-on mark missing"; fi
    if [ -z "$($ADB shell "run-as $PACKAGE cat files/$note" 2>/dev/null | head -1)" ]; then
        say "ok   $note collected"
    else
        fail "$note not collected"
    fi
    if $ADB shell "run-as $PACKAGE cat files/diagnostics.log" 2>/dev/null | tail -6 | grep -q "description"; then
        say "ok   $first opened as a description"
    else
        fail "$first did not open as a description"
    fi
    $ADB shell "run-as $PACKAGE rm -f shared_prefs/crash.xml"
done

say "== result: $failures failure(s)"
[ "$failures" -eq 0 ]
