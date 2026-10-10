#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
daemon="${repo}/sysext/usr/libexec/casdap/casdap-powerd"
unit="${repo}/sysext/usr/lib/systemd/user/casdap-powerd.service"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export STATE="${T}/state"
export CASDAP_RUNTIME_DIR="${T}/rt"

cat > "${T}/bin/evtest" <<'EOF'
#!/usr/bin/bash
script="${STATE}/script-$(basename "$1")"
[[ -f "${script}" ]] || exec sleep 10
ev() { echo "Event: time $(date +%s.%6N), type 1 (EV_KEY), code $1 ($2), value $3"; }
while read -r step; do
    case "${step}" in
        press) ev 116 KEY_POWER 1 ;;
        release) ev 116 KEY_POWER 0 ;;
        repeat) ev 116 KEY_POWER 2 ;;
        other) ev 115 KEY_VOLUMEUP 1 ;;
        profile*) echo "${step#profile }" > "${STATE}/profile" ;;
        sleep*) sleep "${step#sleep }" ;;
    esac
done < "${script}"
sleep 0.3
EOF

cat > "${T}/bin/busctl" <<'EOF'
#!/usr/bin/bash
case "$*" in
    "--system get-property org.armada.Power /org/armada/Power org.armada.Power Profile")
        [[ -s "${STATE}/profile" ]] || exit 1
        echo "s \"$(cat "${STATE}/profile")\""
        ;;
    "--system tree --list org.shadowblip.InputPlumber")
        echo /org/shadowblip/InputPlumber
        echo /org/shadowblip/InputPlumber/CompositeDevice0
        ;;
    *org.shadowblip.Output.ForceFeedback*) echo "busctl $*" >> "${STATE}/buzz" ;;
    *) echo "busctl $*" >> "${STATE}/calls" ;;
esac
EOF

cat > "${T}/bin/casdap-power" <<'EOF'
#!/usr/bin/bash
echo "casdap-power $*" >> "${STATE}/calls"
if [[ "$*" == cpu-apply && -s "${STATE}/apply-sleep" ]]; then
    sleep "$(cat "${STATE}/apply-sleep")"
fi
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
shift 2
echo "$*" >> "${STATE}/log"
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"
export CASDAP_POWERD_LONG_MS=200
export CASDAP_POWERD_DEVICE_WAIT_SEC=0
export CASDAP_POWERD_DEVICE_NAME=casdap-test-pwrkey
export CASDAP_POWERD_POLL_SEC=0.05
export CASDAP_POWERD_BUZZ_MS=20
export CASDAP_POWER_DEBOUNCE_MS=2000
export CASDAP_DEV_INPUT="${T}/nodev" CASDAP_SYS_INPUT="${T}/nosys"

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; echo "--- log" >&2; cat "${STATE}/log" >&2; echo "--- calls" >&2; cat "${STATE}/calls" >&2; failures=$((failures + 1)); }

run_case() {
    case_name=$1
    rm -rf "${STATE}" "${CASDAP_RUNTIME_DIR}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/log"
    printf '%s' "${PROFILE-Balanced}" > "${STATE}/profile"
    printf '%s' "${APPLY_SLEEP:-}" > "${STATE}/apply-sleep"
    if [[ "${OFF:-0}" == 1 ]]; then
        mkdir -p "${CASDAP_RUNTIME_DIR}"
        : > "${CASDAP_RUNTIME_DIR}/off"
    fi
    if [[ "${RECENT:-0}" == 1 ]]; then
        mkdir -p "${CASDAP_RUNTIME_DIR}"
        date +%s%3N > "${CASDAP_RUNTIME_DIR}/last-ms"
    fi
    if [[ "${LOCKED:-0}" == 1 ]]; then
        mkdir -p "${CASDAP_RUNTIME_DIR}"
        ( flock 9; sleep 1.5 ) 9>"${CASDAP_RUNTIME_DIR}/lock" &
        sleep 0.2
    fi
    [[ -z "$2" ]] || printf '%s\n' "$2" | tr ';' '\n' > "${STATE}/script-event0"
    rc=0
    CASDAP_POWERD_DRY_RUN="${3:-0}" "${daemon}" || rc=$?
}

expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_log() { grep -qF -- "$1" "${STATE}/log" || fail "missing log: $1"; }
expect_calls() { [[ "$(grep -c . "${STATE}/calls" || true)" == "$1" ]] || fail "$(grep -c . "${STATE}/calls" || true) calls, expected $1"; }
expect_sequence() {
    local expected
    expected=$(printf '%s\n' "$@")
    [[ "$(cat "${STATE}/calls")" == "${expected}" ]] || fail "calls were:"$'\n'"$(cat "${STATE}/calls")"$'\n'"expected:"$'\n'"${expected}"
}
expect_buzz() {
    local n=0
    if [[ -e "${STATE}/buzz" ]]; then
        n=$(grep -cF -- "CompositeDevice0 org.shadowblip.Output.ForceFeedback Rumble d 1.0" "${STATE}/buzz" || true)
    fi
    [[ "${n}" == "$1" ]] || fail "${n} buzzes, expected $1"
}

toggle="casdap-power toggle"
screen_on="casdap-power screen-on"
apply="casdap-power cpu-apply"
menu="busctl --user call org.casdap.Player /org/casdap/Player org.casdap.Player ShowPowerMenu"

run_case "no device" "press"
[[ "${rc}" == 0 ]] || fail "exit ${rc} without device"
expect_log "not found"

mkdir -p "${T}/sys/event0/device" "${T}/dev"
: > "${T}/dev/event0"
echo casdap-test-pwrkey > "${T}/sys/event0/device/name"
export CASDAP_DEV_INPUT="${T}/dev" CASDAP_SYS_INPUT="${T}/sys"

run_case "short press" "press;sleep 0.05;release"
expect_sequence "${toggle}"
expect_buzz 1
expect_log "watching ${T}/dev/event0 for KEY_POWER"

run_case "short press dry run" "press;sleep 0.05;release" 1
expect_calls 0
expect_buzz 0
expect_log "would: buzz 20"
expect_log "would: ${toggle}"

unset CASDAP_POWERD_BUZZ_MS
run_case "short press dry run with the default tick" "press;sleep 0.05;release" 1
expect_calls 0
expect_log "would: buzz 15"
export CASDAP_POWERD_BUZZ_MS=20

export CASDAP_POWERD_BUZZ_LEVEL=0.3
run_case "short press with a buzz level" "press;sleep 0.05;release"
expect_sequence "${toggle}"
expect_buzz 0
grep -qF -- "CompositeDevice0 org.shadowblip.Output.ForceFeedback Rumble d 0.3" "${STATE}/buzz" || fail "no buzz at level 0.3"
unset CASDAP_POWERD_BUZZ_LEVEL
RECENT=1 run_case "short press within the debounce" "press;sleep 0.05;release"
expect_sequence "${toggle}"
expect_buzz 0
expect_log "tap during a transition or the debounce, no buzz"

RECENT=1 run_case "short press within the debounce dry run" "press;sleep 0.05;release" 1
expect_calls 0
expect_log "would: ${toggle}"
grep -qF -- "would: buzz" "${STATE}/log" && fail "dry run buzzed within the debounce" || true

LOCKED=1 run_case "short press during a transition" "press;sleep 0.05;release"
wait
expect_sequence "${toggle}"
expect_buzz 0
expect_log "tap during a transition or the debounce, no buzz"

run_case "two short presses" "press;sleep 0.05;release;sleep 0.1;press;sleep 0.05;release"
expect_sequence "${toggle}" "${toggle}"
expect_buzz 2

run_case "hold with the screen on" "press;sleep 0.4;release"
expect_sequence "${menu}"
expect_buzz 1
expect_log "hold"
expect_no_call "${toggle}"

OFF=1 run_case "hold with the screen off" "press;sleep 0.4;release"
expect_sequence "${screen_on}" "${menu}"
expect_buzz 1
expect_log "hold while off, screen on first"

OFF=1 run_case "hold with the screen off dry run" "press;sleep 0.4;release" 1
expect_calls 0
expect_buzz 0
expect_log "would: buzz 20"
expect_log "would: ${screen_on}"
expect_log "would: ${menu}"

OFF=1 run_case "short press with the screen off" "press;sleep 0.05;release"
expect_sequence "${toggle}"
expect_buzz 1

run_case "autorepeat keeps the hold" "press;sleep 0.05;repeat;sleep 0.05;repeat;sleep 0.3;release"
expect_sequence "${menu}"
expect_buzz 1

run_case "release without press" "release;sleep 0.05;release"
expect_calls 0
expect_buzz 0

run_case "release then press" "release;press;sleep 0.05;release"
expect_sequence "${toggle}"

run_case "other key" "other;sleep 0.05"
expect_calls 0

run_case "profile change" "sleep 0.3;profile Eco;sleep 0.4"
expect_sequence "${apply}"
expect_log "profile s \"Balanced\" to s \"Eco\""

run_case "profile change dry run" "sleep 0.3;profile Eco;sleep 0.4" 1
expect_calls 0
expect_log "would: ${apply}"

run_case "profile change twice" "sleep 0.3;profile Eco;sleep 0.3;profile Balanced;sleep 0.4"
expect_sequence "${apply}" "${apply}"

run_case "profile unchanged" "sleep 0.5"
expect_calls 0

PROFILE= run_case "profile unreadable then read" "sleep 0.3;profile Balanced;sleep 0.4"
expect_calls 0

run_case "profile change during a hold" "press;sleep 0.05;profile Eco;sleep 0.4;release"
expect_call "${apply}"
expect_call "${menu}"
expect_calls 2

CASDAP_POWERD_LONG_MS=400 APPLY_SLEEP=1 run_case "tap behind a slow profile apply" "sleep 0.3;press;sleep 0.02;profile Eco;sleep 0.2;release;sleep 1.2"
expect_sequence "${apply}" "${toggle}"
expect_buzz 1
expect_no_call "${menu}"

run_case "stream end" "press;sleep 0.05;release"
[[ "${rc}" == 1 ]] || fail "exit ${rc} after stream end, expected 1"
expect_log "event stream ended"

case_name=unit
for line in \
    'PartOf=gamescope-session-plus@casdap.service' \
    'BindsTo=gamescope-session-plus@casdap.service' \
    'After=gamescope-session-plus@casdap.service' \
    'Restart=always' \
    'StartLimitIntervalSec=0'; do
    grep -qxF -- "${line}" "${unit}" || fail "unit lacks ${line}"
done
grep -qE '^ExecStart=/usr/bin/systemd-inhibit --what=handle-power-key:handle-suspend-key --who=casdap-powerd --why="[^"]+" --mode=block /usr/libexec/casdap/casdap-powerd$' "${unit}" || fail "unit ExecStart is not the inhibited daemon"

if command -v systemd-analyze >/dev/null && [[ -e /usr/lib/systemd/user/gamescope-session-plus@.service && -x /usr/libexec/casdap/casdap-powerd ]]; then
    systemd-analyze --user verify "${unit}" || fail "systemd-analyze verify"
else
    echo "skip: systemd-analyze verify (image units or casdap-powerd absent)"
fi

if (( failures )); then
    echo "powerd-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "powerd-test: ok"
