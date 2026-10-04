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
ev() { echo "Event: time 0.0, type 1 (EV_KEY), code 183 (KEY_F13), value $1"; }
while read -r step; do
    case "${step}" in
        press) ev 1 ;;
        release) ev 0 ;;
        repeat) ev 2 ;;
        sleep*) sleep "${step#sleep }" ;;
    esac
done < "${STATE}/script"
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
    printf '%s\n' "$5" | tr ';' '\n' > "${STATE}/script"
    rc=0
    CASDAP_HOTKEYD_DRY_RUN="${6:-0}" "${daemon}" || rc=$?
}

expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_log() { grep -qF -- "$1" "${STATE}/log" || fail "missing log: $1"; }
expect_calls() { [[ "$(grep -c . "${STATE}/calls" || true)" == "$1" ]] || fail "$(grep -c . "${STATE}/calls" || true) calls, expected $1"; }

switch="systemctl start casdap-switch.service"
back="systemctl start casdap-switch-back.service"
playpause="busctl --user call org.mpris.MediaPlayer2.casdap /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player PlayPause"

run_case "no device" game 0 0 "press"
[[ "${rc}" == 0 ]] || fail "exit ${rc} without device"
expect_log "not found"

mkdir -p "${T}/sys/event0/device" "${T}/dev"
: > "${T}/dev/event0"
echo casdap-test-node > "${T}/sys/event0/device/name"
export CASDAP_DEV_INPUT="${T}/dev" CASDAP_SYS_INPUT="${T}/sys"

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

if (( failures )); then
    echo "hotkeyd-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "hotkeyd-test: ok"
