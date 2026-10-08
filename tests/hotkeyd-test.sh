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
        press)
            t=$(date +%s.%6N)
            echo "Event: time ${t}, type 1 (EV_KEY), code 183 (KEY_F13), value 1"
            echo $(( ${t%.*} * 1000 + 10#${t#*.} / 1000 )) >> "${STATE}/pressed"
            ;;
        release) ev 183 KEY_F13 0 ;;
        stale-release)
            t=$(date +%s.%6N)
            echo "Event: time $(( ${t%.*} - 1 )).${t#*.}, type 1 (EV_KEY), code 183 (KEY_F13), value 0"
            ;;
        repeat) ev 183 KEY_F13 2 ;;
        chord)
            cp "${CASDAP_GUIDE_DIR}/down" "${CASDAP_GUIDE_DIR}/chord"
            cat "${CASDAP_GUIDE_DIR}/down" >> "${STATE}/chords"
            ;;
        chord-old) echo $(( $(cat "${CASDAP_GUIDE_DIR}/down") - 1000 )) > "${CASDAP_GUIDE_DIR}/chord" ;;
        peek) cat "${CASDAP_GUIDE_DIR}/down" >> "${STATE}/peeks" ;;
        no-chord) rm -f "${CASDAP_GUIDE_DIR}/chord" ;;
        end) exit 0 ;;
        mark*) echo "${step}" >> "${STATE}/system" ;;
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
if [[ "${1:-}" == --system ]]; then
    echo "busctl $*" >> "${STATE}/system"
    case "$*" in
        *" tree "*)
            printf '%s\n' /org/shadowblip/InputPlumber /org/shadowblip/InputPlumber/CompositeDevice0 \
                /org/shadowblip/InputPlumber/CompositeDevice1 /org/shadowblip/InputPlumber/devices/target/dbus0
            ;;
        *" Rumble "*)
            if [[ -e "${STATE}/rumble-fail" ]]; then
                echo "Call failed: no rumble" >&2
                exit 1
            fi
            ;;
    esac
    exit 0
fi
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
[[ "$1" == set-volume && -s "${STATE}/wpctl-sleep" ]] && sleep "$(cat "${STATE}/wpctl-sleep")"
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
export CASDAP_HOTKEYD_POCKET_LONG_MS=100
export CASDAP_HOTKEYD_CHORD_MS=100
export CASDAP_HOTKEYD_CHORD_GRACE_MS=40
export CASDAP_GUIDE_DIR="${T}/guide"
export CASDAP_HOTKEYD_CONFIRM_MS=1000
export CASDAP_HOTKEYD_TAP_MAX_AGE_MS=600
export CASDAP_HOTKEYD_DOUBLE_MS=250
export CASDAP_HOTKEYD_DEVICE_WAIT_SEC=0
export CASDAP_HOTKEYD_DEVICE_NAME=casdap-test-node
export CASDAP_HOTKEYD_VOLUME_NAMES=casdap-test-volup:casdap-test-voldown
export CASDAP_DEV_INPUT="${T}/nodev" CASDAP_SYS_INPUT="${T}/nosys"
export CASDAP_RUNTIME_DIR="${T}/power"
export CASDAP_SETTINGS_FILE="${T}/settings.ini"

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; echo "--- log" >&2; cat "${STATE}/log" >&2; echo "--- calls" >&2; cat "${STATE}/calls" >&2; echo "--- system" >&2; cat "${STATE}/system" >&2; echo "--- stderr" >&2; cat "${STATE}/stderr" >&2; failures=$((failures + 1)); }

run_case() {
    case_name=$1
    rm -rf "${STATE}" "${CASDAP_GUIDE_DIR}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/log"
    : > "${STATE}/system"
    [[ -z "${RUMBLE_FAIL:-}" ]] || : > "${STATE}/rumble-fail"
    echo "$2" > "${STATE}/session"
    echo "$3" > "${STATE}/title"
    echo "$4" > "${STATE}/plasma"
    echo "${OVERLAY:-inactive}" > "${STATE}/overlay"
    echo "${INTERCEPT:-0}" > "${STATE}/intercept"
    printf '%s' "${BUSCTL_ERR:-}" > "${STATE}/busctl-stderr"
    printf '%s' "${BUSCTL_SLEEP:-}" > "${STATE}/busctl-sleep"
    printf '%s' "${WPCTL_SLEEP:-}" > "${STATE}/wpctl-sleep"
    [[ -z "$5" ]] || printf '%s\n' "$5" | tr ';' '\n' > "${STATE}/script-event0"
    [[ -z "${7:-}" ]] || printf '%s\n' "$7" | tr ';' '\n' > "${STATE}/script-event1"
    [[ "${8:-0}" == 0 ]] || : > "${STATE}/muted"
    rc=0
    CASDAP_HOTKEYD_DRY_RUN="${6:-0}" "${daemon}" 2> "${STATE}/stderr" || rc=$?
}

expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_log() { grep -qF -- "$1" "${STATE}/log" || fail "missing log: $1"; }
expect_calls() { [[ "$(grep -c . "${STATE}/calls" || true)" == "$1" ]] || fail "$(grep -c . "${STATE}/calls" || true) calls, expected $1"; }
expect_call_count() { [[ "$(grep -cF -- "$1" "${STATE}/calls" || true)" == "$2" ]] || fail "$(grep -cF -- "$1" "${STATE}/calls" || true) of '$1', expected $2"; }
expect_first_call() { [[ "$(head -n 1 "${STATE}/calls")" == "$1" ]] || fail "first call is not: $1"; }
expect_last_call() { [[ "$(tail -n 1 "${STATE}/calls")" == "$1" ]] || fail "last call is not: $1"; }
expect_no_log() { grep -qF -- "$1" "${STATE}/log" && fail "unexpected log: $1" || true; }
expect_system_count() { [[ "$(grep -cxF -- "$1" "${STATE}/system" || true)" == "$2" ]] || fail "$(grep -cxF -- "$1" "${STATE}/system" || true) of '$1', expected $2"; }
expect_no_rumble() { grep -qF Rumble "${STATE}/system" && fail "unexpected rumble" || true; }
expect_guide() { [[ "$(cat "${CASDAP_GUIDE_DIR}/$1" 2>/dev/null)" == "$2" ]] || fail "$1 reads '$(cat "${CASDAP_GUIDE_DIR}/$1" 2>/dev/null)', expected '$2'"; }
expect_no_stderr() { [[ ! -s "${STATE}/stderr" ]] || fail "stderr: $(cat "${STATE}/stderr")"; }
expect_no_system() { [[ ! -s "${STATE}/system" ]] || fail "unexpected system bus calls"; }
expect_system_before() {
    local a b
    a=$(grep -nxF -- "$1" "${STATE}/system" | head -n 1 | cut -d: -f1)
    b=$(grep -nxF -- "$2" "${STATE}/system" | head -n 1 | cut -d: -f1)
    if [[ -z "${a}" || -z "${b}" ]] || (( a > b )); then
        fail "'$1' not before '$2'"
    fi
}

switch="systemctl start casdap-switch.service"
back="systemctl start casdap-switch-back.service"
playpause="busctl --user --timeout=2 call org.mpris.MediaPlayer2.casdap /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player PlayPause"
next="busctl --user --timeout=2 call org.mpris.MediaPlayer2.casdap /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player Next"
previous="busctl --user --timeout=2 call org.mpris.MediaPlayer2.casdap /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player Previous"
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
ff="org.shadowblip.Output.ForceFeedback Rumble d"
dev0="busctl --system call org.shadowblip.InputPlumber /org/shadowblip/InputPlumber/CompositeDevice0 ${ff}"
dev1="busctl --system call org.shadowblip.InputPlumber /org/shadowblip/InputPlumber/CompositeDevice1 ${ff}"

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
expect_no_rumble

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

run_case "hold buzzes before release" casdap 0 0 "press;sleep 0.8;mark release;release"
expect_call "${prompt}"
expect_calls 1
expect_system_count "${dev0} 1.0" 1
expect_system_count "${dev0} 0" 1
expect_system_count "${dev1} 1.0" 1
expect_system_count "${dev1} 0" 1
grep -qF "devices/target/dbus0 ${ff}" "${STATE}/system" && fail "rumble on a target path" || true
expect_system_before "${dev0} 1.0" "${dev0} 0"
expect_system_before "${dev0} 0" "mark release"
expect_system_before "${dev1} 0" "mark release"

RUMBLE_FAIL=1 run_case "hold with a failing rumble" casdap 0 0 "press;sleep 0.4;release"
expect_call "${prompt}"
expect_calls 1
expect_system_count "${dev0} 1.0" 1
expect_no_log "failed"

export CASDAP_HOTKEYD_BUZZ_LEVEL=0.3
run_case "hold with a buzz level" game 0 0 "press;sleep 0.4;release"
expect_call "${switch}"
expect_system_count "${dev0} 0.3" 1
expect_system_count "${dev0} 1.0" 0
expect_system_count "${dev0} 0" 1
unset CASDAP_HOTKEYD_BUZZ_LEVEL

run_case "hold in casdap dry run" casdap 0 0 "press;sleep 0.4;release" 1
expect_calls 0
expect_log "would: ${prompt}"
expect_log "would: buzz 15"
expect_no_system

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
expect_no_rumble

run_case "tap in casdap with the screen on is immediate" casdap 0 0 "press;sleep 0.05;release" 0 "sleep 0.2;up;up-release"
expect_first_call "${playpause}"
expect_call_count "${playpause}" 1
expect_no_call "${next}"
expect_calls 5

run_case "hold in casdap with the screen on" casdap 0 0 "press;sleep 0.4;release"
expect_call "${prompt}"
expect_no_call "${previous}"
expect_calls 1

run_case "start writes the guide files" game 0 0 "end"
expect_guide down 0
expect_guide chord 0

run_case "press writes the event time to down" game 0 0 "press;sleep 0.05;peek;release;sleep 0.05;peek"
[[ -s "${STATE}/pressed" && "$(head -n 1 "${STATE}/peeks")" == "$(cat "${STATE}/pressed")" ]] || fail "down $(head -n 1 "${STATE}/peeks"), press $(cat "${STATE}/pressed")"
[[ "$(tail -n 1 "${STATE}/peeks")" == 0 ]] || fail "down not zeroed after the release"

run_case "release after the window writes 0" casdap 0 0 "press;sleep 0.15;release;sleep 0.05;peek"
expect_call "${playpause}"
[[ "$(cat "${STATE}/peeks")" == 0 ]] || fail "down reads $(cat "${STATE}/peeks")"
expect_guide down 0

run_case "chord drops the tap" casdap 0 0 "press;sleep 0.05;chord;release"
expect_no_call "${playpause}"
expect_calls 0
expect_log "chord, tap dropped"
expect_no_log "tap, play pause"
expect_guide down 0

run_case "chord drops the hold" casdap 0 0 "press;sleep 0.05;chord;sleep 0.25;release;sleep 0.05;press;sleep 0.05;release"
expect_no_call "${prompt}"
expect_no_rumble
expect_log "chord, hold dropped"
expect_guide down 0
expect_no_log "hold in casdap"
expect_call_count "${playpause}" 1
expect_calls 1

run_case "an old chord acts" casdap 0 0 "press;sleep 0.05;chord-old;release"
expect_call_count "${playpause}" 1
expect_no_log "chord, tap dropped"

run_case "chord inside the grace drops the tap" casdap 0 0 "press;sleep 0.05;release;sleep 0.02;chord"
expect_no_call "${playpause}"
expect_log "chord, tap dropped"
[[ -s "${STATE}/chords" && "$(cat "${STATE}/chords")" != 0 ]] || fail "down read $(cat "${STATE}/chords") at the chord"

run_case "chord after the grace acts" casdap 0 0 "press;sleep 0.05;release;sleep 0.1;chord"
expect_call_count "${playpause}" 1
expect_log "tap, play pause"
[[ "$(cat "${STATE}/chords")" == 0 ]] || fail "down read $(cat "${STATE}/chords") after the grace"

run_case "release after the chord window acts at once" casdap 0 0 "press;sleep 0.15;release;sleep 0.02;chord"
expect_call_count "${playpause}" 1
expect_no_log "chord, tap dropped"

run_case "new press resolves a due tap first" casdap 0 0 "press;sleep 0.05;release;sleep 0.01;press;sleep 0.3;release"
expect_first_call "${playpause}"
expect_last_call "${prompt}"
expect_call_count "${playpause}" 1
expect_calls 2

CASDAP_HOTKEYD_CHORD_GRACE_MS=150 run_case "volume key during a due tap" casdap 0 0 "press;sleep 0.05;release" 0 "sleep 0.06;up;up-release"
expect_call "${volup}"
expect_call "${emit}"
expect_call_count "${playpause}" 1
expect_calls 5

run_case "stream end with a due tap" casdap 0 0 "press;sleep 0.05;release;end"
[[ "${rc}" == 1 ]] || fail "exit ${rc} after stream end, expected 1"
expect_call_count "${playpause}" 1
expect_log "event stream ended"

run_case "missing chord file" casdap 0 0 "press;sleep 0.05;no-chord;release"
expect_call_count "${playpause}" 1
expect_no_stderr

run_case "screen on 150 ms press is a tap" casdap 0 0 "press;sleep 0.15;release"
expect_call_count "${playpause}" 1
expect_no_call "${prompt}"
expect_calls 1

run_case "screen on 300 ms press shows the prompt" casdap 0 0 "press;sleep 0.3;release"
expect_call "${prompt}"
expect_guide down 0
expect_no_call "${playpause}"
expect_calls 1

OVERLAY=active run_case "game mode ignores a chord" game 0 0 "press;sleep 0.05;chord;release"
expect_call "${toggle}"
expect_calls 1
expect_no_log "chord,"

WPCTL_SLEEP=0.3 run_case "press queued behind a slow wpctl is a tap" casdap 0 0 "sleep 0.02;press;sleep 0.05;release" 0 "up;up-release"
expect_call_count "${playpause}" 1
expect_no_call "${prompt}"
expect_no_log "hold in"
expect_calls 5

run_case "tap logs play pause" casdap 0 0 "press;sleep 0.05;release"
expect_log "tap, play pause"
expect_no_stderr

mkdir -p "${CASDAP_RUNTIME_DIR}"
: > "${CASDAP_RUNTIME_DIR}/off"

run_case "pocket hold" casdap 0 0 "press;sleep 0.4;release"
expect_call "${previous}"
expect_no_call "${screen_on}"
expect_no_call "${prompt}"
expect_no_call "${back}"
expect_calls 1
expect_log "hold, previous"
expect_system_count "${dev0} 1.0" 1
expect_system_count "${dev1} 0" 1

run_case "pocket hold dry run" casdap 0 0 "press;sleep 0.4;release" 1
expect_calls 0
expect_log "would: ${previous}"
expect_log "would: buzz 15"
expect_no_system

run_case "tap in casdap while the screen is off" casdap 0 0 "press;sleep 0.05;release;sleep 0.4"
expect_call "${playpause}"
expect_no_call "${screen_on}"
expect_calls 1

run_case "hold in game while the casdap marker is left" game 0 0 "press;sleep 0.4;release"
expect_call "${switch}"
expect_no_call "${screen_on}"
expect_no_call "${previous}"
expect_calls 1

printf '[General]\npocketControls=false\n' > "${CASDAP_SETTINGS_FILE}"

run_case "pocket hold ignores a pocketControls key" casdap 0 0 "press;sleep 0.4;release"
expect_call "${previous}"
expect_no_call "${screen_on}"
expect_calls 1

rm -f "${CASDAP_SETTINGS_FILE}"

run_case "pocket tap waits for the double tap window" casdap 0 0 "press;sleep 0.05;release;sleep 0.4" 0 "sleep 0.1;up;up-release"
expect_call_count "${playpause}" 1
expect_last_call "${playpause}"
expect_call "${volup}"
expect_calls 5
expect_log "tap, play pause"
expect_no_rumble

run_case "pocket double tap" casdap 0 0 "press;sleep 0.05;release;sleep 0.05;press;sleep 0.05;release;sleep 0.4"
expect_call "${next}"
expect_no_call "${playpause}"
expect_calls 1
expect_log "double tap, next"
expect_system_count "${dev0} 1.0" 2
expect_system_count "${dev0} 0" 2
expect_system_count "${dev1} 1.0" 2
expect_system_count "${dev1} 0" 2

run_case "pocket double tap dry run" casdap 0 0 "press;sleep 0.05;release;sleep 0.05;press;sleep 0.05;release;sleep 0.4" 1
expect_calls 0
expect_log "would: ${next}"
expect_log "would: buzz 15 50 15"
expect_no_system

run_case "pocket taps spaced over the window" casdap 0 0 "press;sleep 0.05;release;sleep 0.4;press;sleep 0.05;release;sleep 0.4"
expect_call_count "${playpause}" 2
expect_no_call "${next}"
expect_calls 2

run_case "pocket tap then hold" casdap 0 0 "press;sleep 0.05;release;sleep 0.05;press;sleep 0.4;release"
expect_first_call "${playpause}"
expect_last_call "${previous}"
expect_no_call "${next}"
expect_calls 2

run_case "pocket 150 ms hold" casdap 0 0 "press;sleep 0.15;release"
expect_call "${previous}"
expect_guide down 0
expect_no_call "${playpause}"
expect_calls 1
expect_system_count "${dev0} 1.0" 1

run_case "pocket 50 ms press is a tap" casdap 0 0 "press;sleep 0.05;release;sleep 0.4"
expect_call "${playpause}"
expect_no_call "${previous}"
expect_calls 1

run_case "pocket ignores a chord" casdap 0 0 "press;sleep 0.03;chord;release;sleep 0.4"
expect_call "${playpause}"
expect_no_log "chord,"

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
expect_no_rumble

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

case_name="now_ms under a comma locale"
mkdir -p "${T}/loc"
if localedef -i de_DE -f UTF-8 "${T}/loc/de_DE.UTF-8" >/dev/null 2>&1; then
    a=$(date +%s%3N)
    b=$(LOCPATH="${T}/loc" LC_ALL=de_DE.UTF-8 bash -c 'source "$1"; now_ms' _ "${daemon}")
    c=$(date +%s%3N)
    [[ "${b}" =~ ^[0-9]+$ ]] && (( a <= b && b <= c )) || { echo "FAIL ${case_name}: ${a} ${b} ${c}" >&2; failures=$((failures + 1)); }
else
    echo "skip: ${case_name}, localedef failed"
fi

if (( failures )); then
    echo "hotkeyd-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "hotkeyd-test: ok"
