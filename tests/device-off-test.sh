#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
off="${repo}/build/device-off"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin" "${T}/sddm"
export STATE="${T}/state"
export CASDAP_EXTENSIONS_DIR="${T}/extensions"
export CASDAP_RUN="${T}/run-casdap"
export CASDAP_SDDM_CONF_DIR="${T}/sddm"
export CASDAP_CONTROLLER_TYPE="${T}/bin/controller-type"
export CASDAP_SESSION_CONTROL="${T}/bin/session-control"
export SUDO_USER=armada

cat > "${T}/bin/id" <<'EOF'
#!/usr/bin/bash
echo 1000
EOF

cat > "${T}/bin/runuser" <<'EOF'
#!/usr/bin/bash
[[ "$1" == -u && "$2" == armada && "$3" == -- && "$4" == env && "$5" == XDG_RUNTIME_DIR=/run/user/1000 ]] \
    || { echo "runuser: unexpected $*" >&2; exit 99; }
shift 5
echo "user $*" >> "${STATE}/calls"
if [[ "$*" == "systemctl --user show -p ActiveState --value gamescope-session-plus@casdap.service" ]]; then
    f="${STATE}/states"
    head -n 1 "${f}"
    if [[ $(wc -l < "${f}") -gt 1 ]]; then
        tail -n +2 "${f}" > "${f}.tmp"
        mv "${f}.tmp" "${f}"
    fi
fi
EOF

cat > "${T}/bin/session-control" <<'EOF'
#!/usr/bin/bash
echo "session-control $*" >> "${STATE}/calls"
[[ -e "${STATE}/sc_fail" ]] && exit 75
printf '[Autologin]\nSession=gamescope-session-steam.desktop\n' > "${CASDAP_SDDM_CONF_DIR}/zz-holo-autologin.conf"
EOF

cat > "${T}/bin/systemctl" <<'EOF'
#!/usr/bin/bash
echo "systemctl $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/systemd-sysext" <<'EOF'
#!/usr/bin/bash
echo "systemd-sysext $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/controller-type" <<'EOF'
#!/usr/bin/bash
echo "controller-type $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/sleep" <<'EOF'
#!/usr/bin/bash
echo "sleep $*" >> "${STATE}/calls"
n=$(cat "${STATE}/settle_after")
(( n < 0 )) && exit 0
n=$((n - 1))
echo "${n}" > "${STATE}/settle_after"
if (( n == 0 )); then
    printf '[Autologin]\nSession=gamescope-session-steam.desktop\n' > "${CASDAP_SDDM_CONF_DIR}/zz-holo-autologin.conf"
fi
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"

failures=0
case_name=

setup() {
    case_name=$1
    rm -rf "${STATE}" "${CASDAP_EXTENSIONS_DIR}" "${CASDAP_RUN}"
    mkdir -p "${STATE}" "${CASDAP_EXTENSIONS_DIR}/casdap/usr" "${CASDAP_EXTENSIONS_DIR}/spanel/usr" "${CASDAP_RUN}"
    : > "${STATE}/calls"
    tr ' ' '\n' <<< "$2" > "${STATE}/states"
    printf '[Autologin]\nSession=%s\n' "$3" > "${T}/sddm/zz-holo-autologin.conf"
    echo "$4" > "${STATE}/settle_after"
}

run() {
    rc=0
    "${off}" > "${STATE}/out" 2> "${STATE}/err" || rc=$?
}

fail() {
    echo "FAIL ${case_name}: $*" >&2
    echo "--- calls" >&2
    cat "${STATE}/calls" >&2
    failures=$((failures + 1))
}

expect_rc() { [[ "${rc}" == "$1" ]] || fail "exit ${rc}, expected $1"; }
expect_call() { grep -qxF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { if grep -qF -- "$1" "${STATE}/calls"; then fail "unexpected call: $1"; fi; }
expect_sleeps() {
    local n
    n=$(grep -c '^sleep ' "${STATE}/calls" || true)
    [[ "${n}" == "$1" ]] || fail "${n} sleeps, expected $1"
}
line_of() { grep -nxF -- "$1" "${STATE}/calls" | head -n 1 | cut -d: -f1; }
expect_before() {
    local a b
    a=$(line_of "$1")
    b=$(line_of "$2")
    [[ -n "${a}" && -n "${b}" && "${a}" -lt "${b}" ]] || fail "'$1' not before '$2'"
}

switch_back="systemctl --no-block start casdap-switch-back.service"
default_gamemode="session-control default-gamemode"
user_stop="user systemctl --user stop casdap-overlay.service casdap-bt.service casdap-pad.service casdap-powerd.service"
hotkeyd_stop="user systemctl --user stop casdap-hotkeyd.service"
user_reload="user systemctl --user daemon-reload"
refresh="systemd-sysext refresh"
system_stop="systemctl stop casdap-guard.service casdap-usb-audio.service"

expect_teardown() {
    [[ -e "${CASDAP_EXTENSIONS_DIR}/casdap" ]] && fail "casdap extension kept"
    [[ -d "${CASDAP_EXTENSIONS_DIR}/spanel/usr" ]] || fail "other extension removed"
    [[ -e "${CASDAP_RUN}" ]] && fail "run directory kept"
    expect_call "${user_stop}"
    expect_call "${hotkeyd_stop}"
    expect_call "${system_stop}"
    expect_call "${refresh}"
    expect_call "systemctl daemon-reload"
    expect_call "${user_reload}"
    expect_call "systemctl restart polkit inputplumber.service"
    expect_call "controller-type apply"
    expect_no_call unmerge
    expect_before "${user_stop}" "${refresh}"
    expect_before "${refresh}" "${user_reload}"
    expect_before "${user_reload}" "${hotkeyd_stop}"
    expect_before "systemctl daemon-reload" "${system_stop}"
    expect_before "systemctl restart polkit inputplumber.service" "controller-type apply"
    return 0
}

setup "casdap session active" "active active deactivating inactive" casdap.desktop 1
run
expect_rc 0
expect_call "${switch_back}"
expect_no_call "${default_gamemode}"
expect_sleeps 2
expect_before "${switch_back}" "${user_stop}"
expect_teardown

setup "game mode" inactive gamescope-session-steam.desktop -1
run
expect_rc 0
expect_no_call "${switch_back}"
expect_no_call "${default_gamemode}"
expect_sleeps 0
expect_teardown

setup "autologin still casdap, session failed" failed casdap.desktop -1
run
expect_rc 0
expect_no_call "${switch_back}"
expect_call "${default_gamemode}"
expect_sleeps 0
expect_before "${default_gamemode}" "${user_stop}"
expect_teardown

setup "default-gamemode fails" inactive casdap.desktop -1
: > "${STATE}/sc_fail"
run
expect_rc 1
expect_call "${default_gamemode}"
grep -q "default-gamemode failed, refusing" "${STATE}/err" || fail "no refusal message: $(cat "${STATE}/err")"
[[ -d "${CASDAP_EXTENSIONS_DIR}/casdap/usr" ]] || fail "casdap extension removed on refusal"
expect_no_call "${user_stop}"
expect_no_call "${refresh}"
expect_no_call "controller-type"

setup "autologin already steam, session deactivating" \
    "deactivating deactivating deactivating deactivating inactive" gamescope-session-steam.desktop -1
run
expect_rc 0
expect_call "${switch_back}"
expect_sleeps 3
expect_teardown

setup "switch never lands" active casdap.desktop -1
run
expect_rc 1
expect_call "${switch_back}"
expect_sleeps 60
grep -q "refusing" "${STATE}/err" || fail "no refusal message: $(cat "${STATE}/err")"
[[ -d "${CASDAP_EXTENSIONS_DIR}/casdap/usr" ]] || fail "casdap extension removed on refusal"
[[ -d "${CASDAP_RUN}" ]] || fail "run directory removed on refusal"
expect_no_call "${user_stop}"
expect_no_call "${hotkeyd_stop}"
expect_no_call "${system_stop}"
expect_no_call "${refresh}"
expect_no_call "controller-type"

if (( failures )); then
    echo "device-off-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "device-off-test: ok"
