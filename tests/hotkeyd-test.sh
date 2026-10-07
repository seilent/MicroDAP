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
ev() { echo "Event: time $(date +%s.%6N), type 1 (EV_KEY), code $1 ($2), value $3"; }
while read -r step; do
    case "${step}" in
        press) ev 183 KEY_F13 1 ;;
        release) ev 183 KEY_F13 0 ;;
        stale-release)
            t=$(date +%s.%6N)
            echo "Event: time $(( ${t%.*} - 1 )).${t#*.}, type 1 (EV_KEY), code 183 (KEY_F13), value 0"
            ;;
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
    "--user is-active casdap-overlay.service") cat "${STATE}/overlay" ;;
    *) echo "systemctl $*" >> "${STATE}/calls" ;;
esac
EOF

cat > "${T}/bin/casdap-intercept" <<'EOF'
#!/usr/bin/bash
echo "casdap-intercept $*" >> "${STATE}/calls"
cat "${STATE}/intercept"
EOF

cat > "${T}/bin/casdap-power" <<'EOF'
#!/usr/bin/bash
echo "casdap-power $*" >> "${STATE}/calls"
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
case "$*" in
    *ToggleOverlay|*ShowSwitchPrompt)
        if [[ -s "${STATE}/busctl-sleep" ]]; then
            sleep "$(cat "${STATE}/busctl-sleep")"
        fi
        if [[ -s "${STATE}/busctl-stderr" ]]; then
            cat "${STATE}/busctl-stderr" >&2
            exit 1
        fi
        ;;
esac
EOF

cat > "${T}/bin/wpctl" <<'EOF'
#!/usr/bin/bash
echo "wpctl $*" >> "${STATE}/calls"
[[ "$*" == "set-mute @DEFAULT_AUDIO_SINK@ 0" ]] && rm -f "${STATE}/muted"
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
export CASDAP_HOTKEYD_TAP_MAX_AGE_MS=600
export CASDAP_HOTKEYD_DEVICE_WAIT_SEC=0
export CASDAP_HOTKEYD_DEVICE_NAME=casdap-test-node
export CASDAP_HOTKEYD_VOLUME_NAMES=casdap-test-volup:casdap-test-voldown
export CASDAP_DEV_INPUT="${T}/nodev" CASDAP_SYS_INPUT="${T}/nosys"
export CASDAP_RUNTIME_DIR="${T}/power"
export CASDAP_SETTINGS_FILE="${T}/settings.ini"

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
    echo "${OVERLAY:-inactive}" > "${STATE}/overlay"
    echo "${INTERCEPT:-0}" > "${STATE}/intercept"
    printf '%s' "${BUSCTL_ERR:-}" > "${STATE}/busctl-stderr"
    printf '%s' "${BUSCTL_SLEEP:-}" > "${STATE}/busctl-sleep"
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
unmute="wpctl set-mute @DEFAULT_AUDIO_SINK@ 0"
emit="busctl --user emit /org/casdap/Hotkeys org.casdap.Hotkeys VolumeChanged d 0.40"
toggle="busctl --user --timeout=2 call org.casdap.Player /org/casdap/Player org.casdap.Player ToggleOverlay"
prompt="busctl --user call org.casdap.Player /org/casdap/Player org.casdap.Player ShowSwitchPrompt"
restart="systemctl --user restart casdap-overlay.service"
intercept="casdap-intercept get"
screen_on="casdap-power screen-on"

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
expect_log "no background player"

OVERLAY=active run_case "tap in game with background player" game 0 0 "press;sleep 0.05;release"
expect_call "${toggle}"
expect_calls 1

OVERLAY=active run_case "tap in game with background player dry run" game 0 0 "press;sleep 0.05;release" 1
expect_calls 0
expect_log "would: ${toggle}"

OVERLAY=active BUSCTL_ERR="Call failed: The name is not activatable" run_case "tap in game while the overlay starts" game 0 0 "press;sleep 0.05;release"
expect_call "${toggle}"
expect_calls 1
expect_no_call "systemctl start"
expect_no_call "restart"
expect_log "overlay player starting"

OVERLAY=active BUSCTL_ERR="Call failed: Connection timed out" INTERCEPT=3 run_case "tap in game with a hung overlay, single timeout" game 0 0 "press;sleep 0.05;release"
expect_call "${toggle}"
expect_no_call "${intercept}"
expect_no_call "${restart}"
expect_calls 1
expect_log "overlay timeout, waiting for the next tap"

OVERLAY=active BUSCTL_ERR="Call failed: Connection timed out" INTERCEPT=3 run_case "tap in game with a hung overlay holding the pad" game 0 0 "press;sleep 0.05;release;sleep 0.5;press;sleep 0.05;release"
expect_call_count "${toggle}" 2
expect_call "${intercept}"
expect_call "${restart}"
expect_calls 4
expect_no_call "systemctl start"
expect_log "overlay timeout 2 in a row, pad held"

OVERLAY=active BUSCTL_ERR="Call failed: Connection timed out" INTERCEPT=0 run_case "tap in game with a hung overlay, pad free" game 0 0 "press;sleep 0.05;release;sleep 0.5;press;sleep 0.05;release"
expect_call_count "${toggle}" 2
expect_call "${intercept}"
expect_no_call "${restart}"
expect_calls 3
expect_log "overlay timeout 2 in a row, pad free"

OVERLAY=active BUSCTL_SLEEP=0.6 run_case "tap during a slow toggle is dropped" game 0 0 "press;sleep 0.05;release;sleep 0.15;press;sleep 0.05;release"
expect_call_count "${toggle}" 1
expect_calls 1
expect_log "tap during a toggle, dropped"
expect_log "ToggleOverlay rc=0 in"

OVERLAY=active run_case "stale tap is dropped" game 0 0 "press;sleep 0.05;stale-release"
expect_no_call "${toggle}"
expect_calls 0
expect_log "ms old, dropped"

OVERLAY=active run_case "fresh taps toggle each time" game 0 0 "press;sleep 0.05;release;sleep 0.3;press;sleep 0.05;release"
expect_call_count "${toggle}" 2
expect_calls 2

OVERLAY=active run_case "hold in game with background player" game 0 0 "press;sleep 0.4;release"
expect_call "${switch}"
expect_calls 1

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
expect_call "${prompt}"
expect_no_call "${back}"
expect_calls 1
expect_log "hold in casdap"

run_case "hold in casdap dry run" casdap 0 0 "press;sleep 0.4;release" 1
expect_calls 0
expect_log "would: ${prompt}"

BUSCTL_ERR="Call failed: The name is not activatable" run_case "hold in casdap without a player" casdap 0 0 "press;sleep 0.4;release"
expect_call "${prompt}"
expect_call "${back}"
expect_calls 2
expect_log "no player, switching back"

BUSCTL_ERR="Call failed: Connection timed out" run_case "hold in casdap with a hung player" casdap 0 0 "press;sleep 0.4;release"
expect_call "${prompt}"
expect_no_call "${back}"
expect_calls 1
expect_log "failed: ShowSwitchPrompt"

run_case "hold then hold in casdap" casdap 0 0 "press;sleep 0.4;release;sleep 0.1;press;sleep 0.4;release"
expect_call_count "${prompt}" 2
expect_calls 2

run_case "tap in casdap" casdap 0 0 "press;sleep 0.05;release"
expect_call "${playpause}"
expect_no_call "${prompt}"
expect_calls 1

mkdir -p "${CASDAP_RUNTIME_DIR}"
: > "${CASDAP_RUNTIME_DIR}/off"

run_case "hold in casdap while the screen is off" casdap 0 0 "press;sleep 0.4;release"
expect_call "${screen_on}"
expect_no_call "${prompt}"
expect_no_call "${back}"
expect_calls 1
expect_log "screen off, turning it on"

run_case "hold in casdap while the screen is off dry run" casdap 0 0 "press;sleep 0.4;release" 1
expect_calls 0
expect_log "would: ${screen_on}"

run_case "tap in casdap while the screen is off" casdap 0 0 "press;sleep 0.05;release"
expect_call "${playpause}"
expect_no_call "${screen_on}"
expect_calls 1

run_case "hold in game while the casdap marker is left" game 0 0 "press;sleep 0.4;release"
expect_call "${switch}"
expect_no_call "${screen_on}"
expect_calls 1

rm -f "${CASDAP_RUNTIME_DIR}/off"

run_case "hold in casdap after the screen came back" casdap 0 0 "press;sleep 0.4;release"
expect_call "${prompt}"
expect_no_call "${screen_on}"
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
expect_call "${unmute}"
expect_call "${volup}"
expect_call "${getvol}"
expect_call "${emit}"
expect_calls 4
expect_log "volume on ${T}/dev/event1 ${T}/dev/event2"

run_case "volume down in casdap" casdap 0 0 "" 0 "down;sleep 0.05;down-release"
expect_call "${voldown}"
expect_no_call "${unmute}"
expect_call "${emit}"
expect_calls 3

printf '[General]\nvolumeStep=10\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volume up with volumeStep 10" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 10%+"
expect_no_call "${volup}"
expect_call "${emit}"
expect_calls 4

printf 'volumeStep=2\r\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volume down with volumeStep 2" casdap 0 0 "" 0 "down;sleep 0.05;down-release"
expect_call "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 2%-"
expect_no_call "${voldown}"
expect_calls 3

printf 'volumeStep=7\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volume up with a bogus volumeStep" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "${volup}"
expect_calls 4

printf 'volumeStep=\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volume up with an empty volumeStep" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "${volup}"

printf 'volumeStep=2\nvolumeStep=10\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volume up with a repeated volumeStep takes the last" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 10%+"

printf 'volumeStep=2\n' > "${CASDAP_SETTINGS_FILE}"
run_case "volumeStep 2 dry run" casdap 0 0 "" 1 "up;sleep 0.05;up-release"
expect_log "would: wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 2%+"
expect_calls 1

rm -f "${CASDAP_SETTINGS_FILE}"
run_case "volume up without a settings file" casdap 0 0 "" 0 "up;sleep 0.05;up-release"
expect_call "${volup}"
expect_calls 4

run_case "volume up in casdap while muted" casdap 0 0 "" 0 "up;sleep 0.05;up-release" 1
expect_call "${unmute}"
expect_call "${emit}"
[[ -e "${STATE}/muted" ]] && fail "still muted after volume up"
expect_calls 4

run_case "volume down in casdap while muted" casdap 0 0 "" 0 "down;sleep 0.05;down-release" 1
expect_no_call "${unmute}"
[[ -e "${STATE}/muted" ]] || fail "volume down unmuted"

run_case "volume autorepeat keeps the press session" casdap 0 0 "" 0 "up;sleep 0.2;session game;up-repeat;sleep 0.05;up-release;sleep 0.05;up;up-release"
expect_call_count "${volup}" 2
expect_call_count "${emit}" 2
expect_call_count "${unmute}" 2
expect_calls 8

run_case "volume in game" game 0 0 "" 0 "up;sleep 0.05;up-release;down;sleep 0.05;down-release"
expect_calls 0

run_case "volume in other" other 0 0 "" 0 "up;sleep 0.05;up-release"
expect_calls 0

run_case "volume up in casdap dry run" casdap 0 0 "" 1 "up;sleep 0.05;up-release"
expect_log "would: ${unmute}"
expect_log "would: ${volup}"
expect_log "would: ${emit}"
expect_call "${getvol}"
expect_calls 1

run_case "volume and f13 interleaved" casdap 0 0 "press;sleep 0.1;release" 0 "sleep 0.05;up;up-release"
expect_call "${playpause}"
expect_call "${volup}"
expect_call "${emit}"
expect_calls 5

if (( failures )); then
    echo "hotkeyd-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "hotkeyd-test: ok"
