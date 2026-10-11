#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
base_root="${CASDAP_BASE_ROOT:-/}"
sc="${base_root%/}/usr/libexec/armada/session-control"
[[ -r "${sc}" ]] || { echo "base session-control not found at ${sc} (set CASDAP_BASE_ROOT)" >&2; exit 1; }
export ARMADA_PLASMA_SESSION_LIB="${base_root%/}/usr/lib/armada/plasma-session-lib"
REAL_FLOCK=$(command -v flock)
export REAL_FLOCK

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
export STATE="${T}/state"
mkdir -p "${T}/bin" "${T}/sddm" "${T}/run" "${STATE}" "${T}/home/.local/share/Steam/steamrtarm64"
python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "${T}/run/bus"
: > "${T}/home/.local/share/Steam/steamrtarm64/steam"
chmod 0755 "${T}/home/.local/share/Steam/steamrtarm64/steam"

cat > "${T}/bin/pop" <<'EOF'
#!/usr/bin/bash
f="${STATE}/$1"
[[ -s "${f}" ]] || { echo inactive; exit 0; }
first=$(head -n 1 "${f}")
if [[ $(wc -l < "${f}") -gt 1 ]]; then
    tail -n +2 "${f}" > "${f}.tmp"
    mv "${f}.tmp" "${f}"
fi
printf '%s\n' "${first}"
EOF

cat > "${T}/bin/sudo" <<'EOF'
#!/usr/bin/bash
while [[ $# -gt 0 ]]; do
    case "$1" in
        -u|-Eu) shift 2 ;;
        env) shift; break ;;
        *) shift ;;
    esac
done
vars=()
while [[ "${1:-}" == *=* ]]; do vars+=("$1"); shift; done
echo "sudo ${vars[*]} :: $*" >> "${STATE}/calls"
at() { tail -n 1 "${ARMADA_SDDM_CONF_DIR}/zz-holo-autologin.conf" > "${STATE}/at_$1" 2>/dev/null || true; }
case "$*" in
    "systemctl --user is-active --quiet -- gamescope-session-plus@steam.service")
        [[ "$(pop steam)" == active ]] ;;
    "systemctl --user is-active --quiet -- gamescope-session-plus@casdap.service")
        [[ "$(pop casdap)" == active ]] ;;
    "systemctl --user stop -- gamescope-session-plus@casdap.service")
        at stop
        echo inactive > "${STATE}/casdap" ;;
    "busctl --user status org.kde.Shutdown")
        [[ "$(cat "${STATE}/plasma")" == 1 ]] ;;
    *"/steam -shutdown")
        at shutdown
        printf '%s\n' "${vars[@]}" > "${STATE}/shutdown_env" ;;
    *qdbus*)
        at logout
        exit 0 ;;
    *)
        echo "unexpected sudo command: $*" >&2; exit 99 ;;
esac
EOF

cat > "${T}/bin/systemctl" <<'EOF'
#!/usr/bin/bash
echo "systemctl $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/loginctl" <<'EOF'
#!/usr/bin/bash
echo "loginctl $*" >> "${STATE}/calls"
case "$1" in
    list-sessions)
        [[ "$2" == --json=short ]] || { echo "loginctl: unexpected $*" >&2; exit 99; }
        echo '[{"session":"1","uid":969,"user":"sddm","seat":"seat0","class":"user"},{"session":"16","uid":1000,"user":"armada","seat":"seat0","class":"user"},{"session":"7","uid":1000,"user":"armada","seat":null,"class":"user"}]' ;;
    show-session)
        [[ "$5" == 16 ]] || { echo "loginctl: unexpected $*" >&2; exit 99; }
        case "$(pop sessions)" in
            active)  echo active ;;
            closing) echo closing ;;
            *)       echo "Could not get properties: No session '16' known" >&2; exit 1 ;;
        esac ;;
esac
EOF

cat > "${T}/bin/sleep" <<'EOF'
#!/usr/bin/bash
echo "sleep $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/flock" <<'EOF'
#!/usr/bin/bash
echo "flock $*" >> "${STATE}/calls"
[[ "$1" == -w ]] && shift 2
exec "${REAL_FLOCK}" -n "$@"
EOF

cat > "${T}/bin/timeout" <<'EOF'
#!/usr/bin/bash
echo "timeout $*" >> "${STATE}/calls"
shift
exec "$@"
EOF

cat > "${T}/bin/controller-type" <<'EOF'
#!/usr/bin/bash
echo "controller-type $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
echo "$*" >> "${STATE}/logger"
EOF

cat > "${T}/bin/getent" <<'EOF'
#!/usr/bin/bash
echo "armada:x:1000:1000::${HOME_DIR}:/bin/bash"
EOF

cat > "${T}/bin/qdbus-qt6" <<'EOF'
#!/usr/bin/bash
exit 0
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"
export HOME_DIR="${T}/home"
export ARMADA_SESSIONS_DIR="${repo}/sysext/usr/lib/armada/sessions.d"
export ARMADA_WAYLAND_SESSIONS_DIR="${repo}/sysext/usr/share/wayland-sessions"
export ARMADA_SDDM_CONF_DIR="${T}/sddm"
export ARMADA_SESSION_RUNTIME_DIR="${T}/run"
export ARMADA_SWITCH_LOCK="${T}/lock/session-switch.lock"
export ARMADA_CONTROLLER_TYPE="${T}/bin/controller-type"
export ARMADA_SHORT_SESSION_TRACKER="${T}/tracker"

failures=0
case_name=

setup() {
    case_name=$1
    rm -rf "${STATE}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/logger"
    tr ' ' '\n' <<< "${2}" > "${STATE}/steam"
    tr ' ' '\n' <<< "${3}" > "${STATE}/casdap"
    echo "${4}" > "${STATE}/plasma"
    tr ' ' '\n' <<< "${5}" > "${STATE}/sessions"
    echo "steam failed" > "${T}/tracker"
    rm -f "${T}/sddm/zz-holo-autologin.conf"
}

run() {
    rc=0
    "${sc}" "$@" > "${STATE}/stdout" || rc=$?
}

fail() {
    echo "FAIL ${case_name}: $*" >&2
    echo "--- calls" >&2
    cat "${STATE}/calls" >&2
    failures=$((failures + 1))
}

expect_rc() { [[ "${rc}" == "$1" ]] || fail "exit ${rc}, expected $1"; }
expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { if grep -qF -- "$1" "${STATE}/calls"; then fail "unexpected call: $1"; fi; }
expect_session() {
    [[ "$(cat "${T}/sddm/zz-holo-autologin.conf")" == "$(printf '[Autologin]\nSession=%s' "$1")" ]] \
        || fail "drop-in is $(tr '\n' ' ' < "${T}/sddm/zz-holo-autologin.conf"), expected $1"
}
expect_at() {
    [[ "$(cat "${STATE}/at_$1" 2>/dev/null)" == "Session=$2" ]] \
        || fail "autologin at $1 was $(cat "${STATE}/at_$1" 2>/dev/null), expected $2"
}
expect_sleeps() {
    local n
    n=$(grep -c '^sleep ' "${STATE}/calls" || true)
    [[ "${n}" -ge "$1" && "${n}" -le "$2" ]] || fail "${n} sleeps, expected $1..$2"
}

sddm_restart="systemctl --no-block restart sddm.service"
steam_shutdown="/steam -shutdown"
casdap_stop="systemctl --user stop -- gamescope-session-plus@casdap.service"
apply="controller-type apply"

setup "list-sessions" inactive inactive 0 gone
run list-sessions
expect_rc 0
[[ "$(cat "${STATE}/stdout")" == casdap ]] || fail "listed $(cat "${STATE}/stdout")"

setup "switch-casdap from game mode" "active inactive" "inactive active" 0 gone
run switch-casdap
expect_rc 0
expect_session casdap.desktop
expect_call "timeout 15 sudo"
expect_call "${steam_shutdown}"
expect_at shutdown casdap.desktop
expect_no_call "${sddm_restart}"
expect_no_call qdbus
expect_no_call "${casdap_stop}"
expect_no_call "${apply}"
if [[ -e "${T}/tracker" ]]; then fail "short session tracker kept"; fi
grep -qxF "HOME=${T}/home" "${STATE}/shutdown_env" || fail "HOME not ${T}/home"
grep -qxF "XDG_RUNTIME_DIR=${T}/run" "${STATE}/shutdown_env" || fail "XDG_RUNTIME_DIR not ${T}/run"
grep -qxF "DBUS_SESSION_BUS_ADDRESS=unix:path=${T}/run/bus" "${STATE}/shutdown_env" || fail "DBUS_SESSION_BUS_ADDRESS wrong"
grep -qxF "LD_LIBRARY_PATH=${T}/home/.local/share/Steam/steamrtarm64" "${STATE}/shutdown_env" || fail "LD_LIBRARY_PATH wrong"
grep -q "^-t session-control -- steam -shutdown HOME=${T}/home XDG_RUNTIME_DIR=${T}/run session=16$" "${STATE}/logger" \
    || fail "logger line missing: $(cat "${STATE}/logger")"

setup "switch-casdap from Plasma" inactive "inactive active" 1 active
run switch-casdap
expect_rc 0
expect_session casdap.desktop
expect_call qdbus
expect_at logout casdap.desktop
expect_no_call "${steam_shutdown}"
expect_no_call "${sddm_restart}"

setup "switch-casdap while casdap runs" inactive active 0 active
run switch-casdap
expect_rc 0
expect_session casdap.desktop
expect_no_call "${casdap_stop}"
expect_no_call "${sddm_restart}"

setup "switch-gamemode from casdap" "inactive active" active 0 active
run switch-gamemode
expect_rc 0
expect_session gamescope-session-steam.desktop
expect_call "${casdap_stop}"
expect_at stop gamescope-session-steam.desktop
expect_call "timeout 20 ${T}/bin/${apply}"
expect_no_call qdbus
expect_no_call "${sddm_restart}"
[[ "$(cat "${STATE}/casdap")" == inactive ]] || fail "casdap unit still active"

setup "switch-desktop from casdap" inactive active 0 active
mkdir -p "${T}/proc/101"
echo "0::/user.slice/user-1000.slice/user@1000.service/session.slice/plasma-plasmashell.service" > "${T}/proc/101/cgroup"
cat > "${T}/bin/pgrep" <<'EOF'
#!/usr/bin/bash
echo 101
EOF
chmod 0755 "${T}/bin/pgrep"
ARMADA_PROC_DIR="${T}/proc" run switch-desktop
expect_rc 0
expect_session armada-plasma.desktop
expect_call "${casdap_stop}"
expect_call "${apply}"
expect_no_call "${sddm_restart}"

setup "switch-gamemode with no Plasma owner" "inactive active" inactive 0 active
run switch-gamemode
expect_rc 0
expect_call "busctl --user status org.kde.Shutdown"
expect_no_call qdbus
expect_call "${sddm_restart}"

setup "default-casdap" active inactive 1 active
run default-casdap
expect_rc 0
expect_session casdap.desktop
expect_call "systemctl reset-failed sddm.service"
expect_no_call "${sddm_restart}"
expect_no_call sudo

setup "switch-casdap with Steam unit inactive" inactive "inactive active" 0 active
run switch-casdap
expect_rc 0
expect_no_call "${steam_shutdown}"
expect_call "${sddm_restart}"
[[ -e "${T}/tracker" ]] || fail "short session tracker removed"

setup "switch-casdap session closing" "active inactive" "inactive active" 0 "active closing"
run switch-casdap
expect_rc 0
expect_call "${steam_shutdown}"
expect_no_call "${sddm_restart}"

setup "switch-casdap session still active after wait" "active inactive" "inactive active" 0 active
run switch-casdap
expect_rc 0
expect_call "${sddm_restart}"
expect_sleeps 10 10
[[ -e "${T}/tracker" ]] || fail "short session tracker removed"

setup "switch-casdap Steam unit never stops" active "inactive active" 0 active
run switch-casdap
expect_rc 0
expect_call "${sddm_restart}"
expect_sleeps 20 20

setup "unknown verb" inactive inactive 0 active
run bogus 2>/dev/null
expect_rc 1
if [[ -e "${T}/sddm/zz-holo-autologin.conf" ]]; then fail "drop-in written for unknown verb"; fi

if (( failures )); then
    echo "session-control-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "session-control-test: ok"
