#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
power="${repo}/sysext/usr/libexec/casdap/casdap-power"
dropin="${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@casdap.service.d/30-casdap-power.conf"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export STATE="${T}/state"
export CASDAP_RUNTIME_DIR="${T}/rt"
export CASDAP_SETTINGS_FILE="${T}/settings.ini"
export CASDAP_CPU_BEFORE_FILE="${T}/cpu-before"
export CASDAP_POWER_DEBOUNCE_MS=2000

cat > "${T}/bin/busctl" <<'EOF'
#!/usr/bin/bash
case "$*" in
    "--user get-property org.casdap.Player /org/casdap/Player org.casdap.Player PlaybackStatus")
        [[ -s "${STATE}/status" ]] || exit 1
        echo "s \"$(cat "${STATE}/status")\""
        ;;
    "--system tree --list org.bluez")
        echo /org/bluez/hci0
        echo /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
        [[ -e "${STATE}/transport" ]] && echo /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/sep1/fd0
        ;;
    "--system get-property org.bluez /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/sep1/fd0 org.bluez.MediaTransport1 State")
        echo "s \"$(cat "${STATE}/transport")\""
        ;;
    *SetScreenOff*)
        echo "busctl $*" >> "${STATE}/calls"
        [[ -e "${CASDAP_RUNTIME_DIR}/off" ]] && echo 1 >> "${STATE}/marker-at-call" || echo 0 >> "${STATE}/marker-at-call"
        [[ -e "${STATE}/no-player" ]] && { echo "Call failed: The name is not activatable" >&2; exit 1; }
        ;;
    *) echo "busctl $*" >> "${STATE}/calls" ;;
esac
exit 0
EOF

cat > "${T}/bin/systemctl" <<'EOF'
#!/usr/bin/bash
case "$*" in
    "--user is-active gamescope-session-plus@casdap.service")
        cat "${STATE}/session"
        [[ "$(cat "${STATE}/session")" == active ]]
        ;;
    "--user stop casdap-idle.timer")
        echo "systemctl $*" >> "${STATE}/calls"
        [[ -e "${STATE}/idle-timer" ]] || { echo "Failed to stop casdap-idle.timer: Unit casdap-idle.timer not loaded." >&2; exit 5; }
        rm -f "${STATE}/idle-timer"
        ;;
    *)
        echo "systemctl $*" >> "${STATE}/calls"
        [[ -s "${STATE}/fail-unit" && "$*" == *"$(cat "${STATE}/fail-unit")"* ]] && exit 1
        ;;
esac
exit 0
EOF

cat > "${T}/bin/systemd-run" <<'EOF'
#!/usr/bin/bash
echo "systemd-run $*" >> "${STATE}/calls"
: > "${STATE}/idle-timer"
EOF

cat > "${T}/bin/nmcli" <<'EOF'
#!/usr/bin/bash
echo "nmcli $*" >> "${STATE}/calls"
[[ -e "${STATE}/nmcli-fails" ]] && exit 4
exit 0
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
shift 2
echo "$*" >> "${STATE}/log"
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; echo "--- log" >&2; cat "${STATE}/log" >&2; echo "--- calls" >&2; cat "${STATE}/calls" >&2; failures=$((failures + 1)); }

begin() {
    case_name=$1
    rm -rf "${STATE}" "${CASDAP_RUNTIME_DIR}" "${CASDAP_SETTINGS_FILE}" "${CASDAP_CPU_BEFORE_FILE}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/log"
    echo "${2:-active}" > "${STATE}/session"
}

run() {
    rc=0
    "${power}" "$@" || rc=$?
}

expect_rc() { [[ "${rc}" == "$1" ]] || fail "exit ${rc}, expected $1"; }
expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_log() { grep -qF -- "$1" "${STATE}/log" || fail "missing log: $1"; }
expect_calls() { [[ "$(grep -c . "${STATE}/calls" || true)" == "$1" ]] || fail "$(grep -c . "${STATE}/calls" || true) calls, expected $1"; }
expect_sequence() {
    local expected
    expected=$(printf '%s\n' "$@")
    [[ "$(cat "${STATE}/calls")" == "${expected}" ]] || fail "calls were:"$'\n'"$(cat "${STATE}/calls")"$'\n'"expected:"$'\n'"${expected}"
}
expect_marker() { [[ -e "${CASDAP_RUNTIME_DIR}/$1" ]] || fail "marker $1 missing"; }
expect_no_marker() { [[ ! -e "${CASDAP_RUNTIME_DIR}/$1" ]] || fail "marker $1 present"; }
marker() { mkdir -p "${CASDAP_RUNTIME_DIR}"; : > "${CASDAP_RUNTIME_DIR}/$1"; }

off_true="busctl --user --timeout=2 call org.casdap.Player /org/casdap/Player org.casdap.Player SetScreenOff b true"
off_false="busctl --user --timeout=2 call org.casdap.Player /org/casdap/Player org.casdap.Player SetScreenOff b false"
bl_off="systemctl start casdap-priv@backlight-off.service"
bl_on="systemctl start casdap-priv@backlight-on.service"
cpu_off="systemctl start casdap-priv@cpu-off.service"
cpu_dap="systemctl start casdap-priv@cpu-dap.service"
cpu_restore="systemctl start casdap-priv@cpu-restore.service"
wifi_off="nmcli radio wifi off"
wifi_on="nmcli radio wifi on"
stop_timer="systemctl --user stop casdap-idle.timer"
arm_timer="systemd-run --user --unit=casdap-idle --on-active=60 --on-unit-inactive=60 --timer-property=AccuracySec=1min /usr/libexec/casdap/casdap-power idle-check"

begin "screen-off"
run screen-off
expect_rc 0
expect_sequence "${off_true}" "${bl_off}" "${cpu_off}" "${stop_timer}" "${arm_timer}"
expect_marker off
expect_no_marker wifi
[[ "$(cat "${STATE}/marker-at-call")" == 1 ]] || fail "off marker written after SetScreenOff"
[[ -e "${STATE}/idle-timer" ]] || fail "idle timer not armed"

begin "screen-off twice"
run screen-off
run screen-off
expect_rc 0
expect_marker off
[[ "$(grep -cF -- "${off_true}" "${STATE}/calls")" == 2 ]] || fail "second screen-off skipped SetScreenOff"
[[ "$(grep -cF -- "${arm_timer}" "${STATE}/calls")" == 2 ]] || fail "second screen-off skipped the timer"
[[ "$(grep -cF -- "${stop_timer}" "${STATE}/calls")" == 2 ]] || fail "second screen-off skipped the stale timer stop"

begin "screen-off with a stale timer"
: > "${STATE}/idle-timer"
run screen-off
expect_rc 0
expect_call "${stop_timer}"
expect_call "${arm_timer}"

begin "screen-off without a player"
: > "${STATE}/no-player"
run screen-off
expect_rc 0
expect_call "${off_true}"
expect_call "${bl_off}"
expect_call "${cpu_off}"
expect_call "${arm_timer}"
expect_log "failed: ${off_true}"
expect_marker off

begin "screen-off with a failing priv start"
echo cpu-off > "${STATE}/fail-unit"
run screen-off
expect_rc 0
expect_call "${cpu_off}"
expect_call "${arm_timer}"
expect_log "failed: ${cpu_off}"

begin "screen-off with wifi off"
printf '[General]\nwifiOffScreenOff=true\nsuspendMinutes=10\n' > "${CASDAP_SETTINGS_FILE}"
run screen-off
expect_rc 0
expect_sequence "${off_true}" "${bl_off}" "${cpu_off}" "${wifi_off}" "${stop_timer}" "${arm_timer}"
expect_marker wifi

begin "screen-off twice with wifi off"
printf 'wifiOffScreenOff=true\r\n' > "${CASDAP_SETTINGS_FILE}"
run screen-off
run screen-off
expect_rc 0
[[ "$(grep -cF -- "${wifi_off}" "${STATE}/calls")" == 1 ]] || fail "wifi turned off twice"
expect_marker wifi

begin "screen-off with wifi kept"
printf 'wifiOffScreenOff=false\n' > "${CASDAP_SETTINGS_FILE}"
run screen-off
expect_no_call "nmcli"
expect_no_marker wifi

begin "screen-off with nmcli denied"
printf 'wifiOffScreenOff=true\n' > "${CASDAP_SETTINGS_FILE}"
: > "${STATE}/nmcli-fails"
run screen-off
expect_rc 0
expect_call "${wifi_off}"
expect_no_marker wifi
expect_log "failed: nmcli radio wifi off"
expect_call "${arm_timer}"

begin "screen-off outside the session" inactive
run screen-off
expect_rc 1
expect_calls 0
expect_no_marker off
expect_log "screen-off outside the casdap session (inactive)"

begin "screen-off while deactivating" deactivating
run screen-off
expect_rc 1
expect_calls 0

begin "screen-on with the marker"
marker off
: > "${STATE}/idle-timer"
: > "${CASDAP_CPU_BEFORE_FILE}"
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${cpu_dap}" "${bl_on}" "${off_false}"
expect_no_marker off
[[ -e "${STATE}/idle-timer" ]] && fail "idle timer left armed" || true
[[ "$(cat "${CASDAP_RUNTIME_DIR}/screen-on-ms")" =~ ^[0-9]{13}$ ]] || fail "screen-on-ms is '$(cat "${CASDAP_RUNTIME_DIR}/screen-on-ms")'"

begin "screen-on with the wifi marker"
marker off
marker wifi
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${wifi_on}" "${cpu_dap}" "${bl_on}" "${off_false}"
expect_no_marker wifi
expect_no_marker off

begin "screen-on without the marker and a saved floor"
: > "${CASDAP_CPU_BEFORE_FILE}"
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${cpu_restore}" "${cpu_dap}" "${bl_on}" "${off_false}"

begin "screen-on without the marker and no saved floor"
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${cpu_dap}" "${bl_on}" "${off_false}"

begin "screen-on twice"
marker off
run screen-on
run screen-on
expect_rc 0
[[ "$(grep -cF -- "${cpu_dap}" "${STATE}/calls")" == 2 ]] || fail "second screen-on skipped cpu-dap"
expect_no_call "${cpu_restore}"
expect_no_marker off

begin "screen-on while deactivating" deactivating
marker off
marker wifi
: > "${CASDAP_CPU_BEFORE_FILE}"
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${wifi_on}" "${cpu_restore}" "${bl_on}"
expect_no_marker off
expect_no_marker wifi

begin "screen-on while inactive" inactive
run screen-on
expect_rc 0
expect_sequence "${stop_timer}" "${cpu_restore}" "${bl_on}"

begin "screen-on without a player"
marker off
: > "${STATE}/no-player"
run screen-on
expect_rc 0
expect_call "${off_false}"
expect_log "failed: ${off_false}"
expect_no_marker off

begin "toggle to off"
mkdir -p "${CASDAP_RUNTIME_DIR}"
echo 1000 > "${CASDAP_RUNTIME_DIR}/screen-on-ms"
run toggle
expect_rc 0
expect_call "${off_true}"
expect_call "${cpu_off}"
expect_marker off

begin "toggle to on"
marker off
run toggle
expect_rc 0
expect_call "${off_false}"
expect_call "${cpu_dap}"
expect_no_marker off

begin "toggle within the debounce"
mkdir -p "${CASDAP_RUNTIME_DIR}"
date +%s%3N > "${CASDAP_RUNTIME_DIR}/screen-on-ms"
run toggle
expect_rc 0
expect_calls 0
expect_no_marker off
expect_log "toggle within 2000 ms of screen on, ignored"

begin "toggle with a corrupt screen-on-ms"
mkdir -p "${CASDAP_RUNTIME_DIR}"
echo bogus > "${CASDAP_RUNTIME_DIR}/screen-on-ms"
run toggle
expect_rc 0
expect_marker off

begin "toggle after screen-on within the debounce"
marker off
run screen-on
run toggle
expect_rc 0
expect_no_call "${off_true}"
expect_no_marker off
expect_log "toggle within 2000 ms of screen on, ignored"

begin "toggle outside the session" inactive
run toggle
expect_rc 1
expect_calls 0
expect_log "toggle outside the casdap session (inactive)"

begin "toggle to on outside the session" inactive
marker off
run toggle
expect_rc 1
expect_calls 0
expect_marker off

begin "cpu-apply with the marker"
marker off
run cpu-apply
expect_rc 0
expect_sequence "${cpu_off}"

begin "cpu-apply without the marker"
run cpu-apply
expect_rc 0
expect_sequence "${cpu_dap}"

begin "cpu-apply outside the session" inactive
run cpu-apply
expect_rc 1
expect_calls 0

begin "idle-check while playing"
marker idle-since
echo Playing > "${STATE}/status"
run idle-check
expect_rc 0
expect_calls 0
expect_no_marker idle-since

begin "idle-check with a phone transport"
marker idle-since
echo Paused > "${STATE}/status"
echo active > "${STATE}/transport"
run idle-check
expect_rc 0
expect_calls 0
expect_no_marker idle-since

begin "idle-check with an idle transport"
echo Paused > "${STATE}/status"
echo idle > "${STATE}/transport"
run idle-check
expect_rc 0
expect_calls 0
expect_marker idle-since
[[ "$(cat "${CASDAP_RUNTIME_DIR}/idle-since")" =~ ^[0-9]{10}$ ]] || fail "idle-since is '$(cat "${CASDAP_RUNTIME_DIR}/idle-since")'"

begin "idle-check paused without a file"
echo Paused > "${STATE}/status"
run idle-check
expect_rc 0
expect_calls 0
expect_marker idle-since

begin "idle-check paused twice keeps the first time"
echo Stopped > "${STATE}/status"
mkdir -p "${CASDAP_RUNTIME_DIR}"
since=$(( $(date +%s) - 300 ))
echo "${since}" > "${CASDAP_RUNTIME_DIR}/idle-since"
run idle-check
expect_rc 0
expect_calls 0
[[ "$(cat "${CASDAP_RUNTIME_DIR}/idle-since")" == "${since}" ]] || fail "idle-since rewritten"

begin "idle-check past suspendMinutes"
echo Paused > "${STATE}/status"
mkdir -p "${CASDAP_RUNTIME_DIR}"
echo $(( $(date +%s) - 601 )) > "${CASDAP_RUNTIME_DIR}/idle-since"
run idle-check
expect_rc 0
expect_sequence "systemctl suspend"
expect_log "idle for 10 min, suspending"
expect_no_marker idle-since

begin "idle-check past a custom suspendMinutes"
printf 'suspendMinutes=5\n' > "${CASDAP_SETTINGS_FILE}"
echo Paused > "${STATE}/status"
mkdir -p "${CASDAP_RUNTIME_DIR}"
echo $(( $(date +%s) - 301 )) > "${CASDAP_RUNTIME_DIR}/idle-since"
run idle-check
expect_rc 0
expect_sequence "systemctl suspend"

begin "idle-check under a custom suspendMinutes"
printf 'suspendMinutes=30\n' > "${CASDAP_SETTINGS_FILE}"
echo Paused > "${STATE}/status"
mkdir -p "${CASDAP_RUNTIME_DIR}"
echo $(( $(date +%s) - 601 )) > "${CASDAP_RUNTIME_DIR}/idle-since"
run idle-check
expect_rc 0
expect_calls 0
expect_marker idle-since

begin "idle-check with suspend off"
printf 'suspendMinutes=0\n' > "${CASDAP_SETTINGS_FILE}"
echo Paused > "${STATE}/status"
marker idle-since
run idle-check
expect_rc 0
expect_calls 0
expect_no_marker idle-since

begin "idle-check without a player"
run idle-check
expect_rc 0
expect_calls 0
expect_marker idle-since

begin "idle-check outside the session" inactive
echo Paused > "${STATE}/status"
run idle-check
expect_rc 0
expect_calls 0
expect_no_marker idle-since

begin "suspend"
run suspend
expect_rc 0
expect_sequence "systemctl suspend"

begin "poweroff"
run poweroff
expect_rc 0
expect_sequence "systemctl poweroff"

for verb in bogus "" "screen-off;id" "screen-on " "toggle --help" SCREEN-OFF reboot; do
    begin "verb '${verb}'"
    run "${verb}"
    expect_rc 1
    expect_calls 0
    expect_no_marker off
done

begin "no verb"
run
expect_rc 1
expect_calls 0

begin "two verbs"
run screen-off screen-on
expect_rc 1
expect_calls 0

begin "drop in"
grep -qxF 'ExecStopPost=/usr/libexec/casdap/casdap-power screen-on' "${dropin}" || fail "drop in lacks ExecStopPost screen-on"
grep -qxF 'Wants=casdap-powerd.service' "${dropin}" || fail "drop in lacks Wants=casdap-powerd.service"

if (( failures )); then
    echo "power-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "power-test: ok"
