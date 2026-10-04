#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
daemon="${repo}/sysext/usr/libexec/casdap/casdap-hotkeyd"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export STATE="${T}/state"

cat > "${T}/bin/evtest" <<'EOF'
#!/usr/bin/bash
script="${STATE}/script-$(basename "$1")"
[[ -f "${script}" ]] || exec sleep 10
ev() { echo "Event: time 0.0, type 1 (EV_KEY), code $1 ($2), value $3"; }
while read -r step; do
    case "${step}" in
        press) ev 183 KEY_F13 1 ;;
        release) ev 183 KEY_F13 0 ;;
        repeat) ev 183 KEY_F13 2 ;;
        up) ev 115 KEY_VOLUMEUP 1 ;;
        up-repeat) ev 115 KEY_VOLUMEUP 2 ;;
        up-release) ev 115 KEY_VOLUMEUP 0 ;;
        down) ev 114 KEY_VOLUMEDOWN 1 ;;
        down-release) ev 114 KEY_VOLUMEDOWN 0 ;;
        session*) echo "${step#session }" > "${STATE}/session" ;;
        sleep*) sleep "${step#sleep }" ;;
    esac
done < "${script}"
sleep 0.3
EOF

cat > "${T}/bin/systemctl" <<'EOF'
#!/usr/bin/bash
case "$*" in
    "--user is-active --quiet gamescope-session-plus@casdap.service") [[ "$(cat "${STATE}/session")" == casdap ]] ;;
    "--user is-active --quiet gamescope-session-plus@steam.service")  [[ "$(cat "${STATE}/session")" == game ]] ;;
    *) echo "systemctl $*" >> "${STATE}/calls" ;;
esac
EOF

cat > "${T}/bin/pgrep" <<'EOF'
#!/usr/bin/bash
case "$*" in
    *SteamLaunch*) [[ "$(cat "${STATE}/title")" == 1 ]] ;;
    *plasmashell*) [[ "$(cat "${STATE}/plasma")" == 1 ]] ;;
    *) exit 1 ;;
esac
EOF

cat > "${T}/bin/busctl" <<'EOF'
#!/usr/bin/bash
echo "busctl $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/wpctl" <<'EOF'
#!/usr/bin/bash
echo "wpctl $*" >> "${STATE}/calls"
if [[ "$1" == get-volume ]]; then
    if [[ -e "${STATE}/muted" ]]; then
        echo "Volume: 0.40 [MUTED]"
    else
        echo "Volume: 0.40"
    fi
fi
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
shift 2
echo "$*" >> "${STATE}/log"
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"
export CASDAP_HOTKEYD_LONG_MS=200
export CASDAP_HOTKEYD_CONFIRM_MS=1000
export CASDAP_HOTKEYD_DEVICE_WAIT_SEC=0
export CASDAP_HOTKEYD_DEVICE_NAME=casdap-test-node
export CASDAP_HOTKEYD_VOLUME_NAMES=casdap-test-volup:casdap-test-voldown
export CASDAP_DEV_INPUT="${T}/nodev" CASDAP_SYS_INPUT="${T}/nosys"

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; echo "--- log" >&2; cat "${STATE}/log" >&2; echo "--- calls" >&2; cat "${STATE}/calls" >&2; failures=$((failures + 1)); }

run_case() {
    case_name=$1
    rm -rf "${STATE}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/log"
    echo "$2" > "${STATE}/session"
    echo "$3" > "${STATE}/title"
    echo "$4" > "${STATE}/plasma"
    [[ -z "$5" ]] || printf '%s\n' "$5" | tr ';' '\n' > "${STATE}/script-event0"
    [[ -z "${7:-}" ]] || printf '%s\n' "$7" | tr ';' '\n' > "${STATE}/script-event1"
    [[ "${8:-0}" == 0 ]] || : > "${STATE}/muted"
    rc=0
    CASDAP_HOTKEYD_DRY_RUN="${6:-0}" "${daemon}" || rc=$?
}

expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_log() { grep -qF -- "$1" "${STATE}/log" || fail "missing log: $1"; }
expect_calls() { [[ "$(grep -c . "${STATE}/calls" || true)" == "$1" ]] || fail "$(grep -c . "${STATE}/calls" || true) calls, expected $1"; }
expect_call_count() { [[ "$(grep -cF -- "$1" "${STATE}/calls" || true)" == "$2" ]] || fail "$(grep -cF -- "$1" "${STATE}/calls" || true) of '$1', expected $2"; }

switch="systemctl start casdap-switch.service"
back="systemctl start casdap-switch-back.service"
playpause="busctl --user call org.mpris.MediaPlayer2.casdap /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player PlayPause"
volup="wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+"
voldown="wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%-"
getvol="wpctl get-volume @DEFAULT_AUDIO_SINK@"
emit="busctl --user emit /org/casdap/Hotkeys org.casdap.Hotkeys VolumeChanged d 0.40"

run_case "no device" game 0 0 "press"
[[ "${rc}" == 0 ]] || fail "exit ${rc} without device"
expect_log "not found"

mkdir -p "${T}/sys/event0/device" "${T}/dev"
: > "${T}/dev/event0"
echo casdap-test-node > "${T}/sys/event0/device/name"
export CASDAP_DEV_INPUT="${T}/dev" CASDAP_SYS_INPUT="${T}/sys"

run_case "tap in casdap without volume nodes" casdap 0 0 "press;sleep 0.05;release"
expect_call "${playpause}"
expect_calls 1
expect_log "volume node casdap-test-volup not found"
expect_log "volume node casdap-test-voldown not found"
expect_log "volume on none"

mkdir -p "${T}/sys/event1/device" "${T}/sys/event2/device"
: > "${T}/dev/event1"
: > "${T}/dev/event2"
echo casdap-test-volup > "${T}/sys/event1/device/name"
echo casdap-test-voldown > "${T}/sys/event2/device/name"

run_case "tap in game" game 0 0 "press;sleep 0.05;release"
expect_calls 0
expect_log "tap in game, ignored"

run_case "hold in game" game 0 0 "press;sleep 0.4;release"
expect_call "${switch}"
expect_calls 1
expect_log "hold in game"

run_case "hold in game dry run" game 0 0 "press;sleep 0.4;release" 1
expect_calls 0
expect_log "would: ${switch}"

run_case "hold in game with title, single" game 1 0 "press;sleep 0.4;release"
expect_calls 0
expect_log "hold ignored, title running"

run_case "hold in game with title, confirmed" game 1 0 "press;sleep 0.4;release;sleep 0.2;press;sleep 0.4;release"
expect_call "${switch}"
expect_calls 1

run_case "hold in game with title, too late" game 1 0 "press;sleep 0.4;release;sleep 1.2;press;sleep 0.4;release"
expect_calls 0

run_case "hold in casdap" casdap 0 0 "press;sleep 0.4;release"
expect_call "${back}"
expect_calls 1

run_case "tap in casdap" casdap 0 0 "press;sleep 0.05;release"
expect_call "${playpause}"
expect_calls 1

run_case "hold in other with plasma" other 0 1 "press;sleep 0.4;release"
expect_call "${switch}"
expect_calls 1

run_case "hold in other without plasma" other 0 0 "press;sleep 0.4;release"
expect_calls 0
expect_log "unknown session"

run_case "autorepeat does not reclassify" game 0 0 "press;sleep 0.05;repeat;sleep 0.05;repeat;sleep 0.3;release"
expect_call "${switch}"
expect_calls 1

run_case "release without press" game 0 0 "release;press;sleep 0.05;release"
expect_calls 0

run_case "stream end" game 0 0 "press;sleep 0.05;release"
[[ "${rc}" == 1 ]] || fail "exit ${rc} after stream end, expected 1"
expect_log "event stream ended"

run_case "volume up in casdap" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "${volup}"
expect_call "${getvol}"
expect_call "${emit}"
expect_calls 3
expect_log "volume on ${T}/dev/event1 ${T}/dev/event2"

run_case "volume down in casdap" casdap 0 0 "" 0 "down;sleep 0.05;down-release"
expect_call "${voldown}"
expect_call "${emit}"
expect_calls 3

run_case "volume up in casdap while muted" casdap 0 0 "" 0 "up;sleep 0.05;up-release" 1
expect_call "${emit}"
expect_calls 3

run_case "volume autorepeat keeps the press session" casdap 0 0 "" 0 "up;sleep 0.2;session game;up-repeat;sleep 0.05;up-release;sleep 0.05;up;up-release"
expect_call_count "${volup}" 2
expect_call_count "${emit}" 2
expect_calls 6

run_case "volume in game" game 0 0 "" 0 "up;sleep 0.05;up-release;down;sleep 0.05;down-release"
expect_calls 0

run_case "volume in other" other 0 0 "" 0 "up;sleep 0.05;up-release"
expect_calls 0

run_case "volume up in casdap dry run" casdap 0 0 "" 1 "up;sleep 0.05;up-release"
expect_log "would: ${volup}"
expect_log "would: ${emit}"
expect_call "${getvol}"
expect_calls 1

run_case "volume and f13 interleaved" casdap 0 0 "press;sleep 0.1;release" 0 "sleep 0.05;up;up-release"
expect_call "${playpause}"
expect_call "${volup}"
expect_call "${emit}"
expect_calls 4

if (( failures )); then
    echo "hotkeyd-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "hotkeyd-test: ok"
