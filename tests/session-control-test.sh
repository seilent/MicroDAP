#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
base_root="${CASDAP_BASE_ROOT:-/}"
base="${base_root%/}/usr/libexec/armada/session-control"
[[ -r "${base}" ]] || { echo "base session-control not found at ${base} (set CASDAP_BASE_ROOT)" >&2; exit 1; }

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
export STATE="${T}/state"
mkdir -p "${T}/usr/libexec/armada" "${T}/bin" "${T}/sddm" "${T}/run" "${STATE}" \
    "${T}/home/.local/share/Steam/steamrtarm64"
cp "${base}" "${T}/usr/libexec/armada/session-control"
(cd "${T}" && patch -s --no-backup-if-mismatch -p1 < "${repo}/patches/armada/usr/libexec/armada/session-control.patch")
sc="${T}/usr/libexec/armada/session-control"
python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "${T}/run/bus"
: > "${T}/home/.local/share/Steam/steamrtarm64/steam"
chmod 0755 "${T}/home/.local/share/Steam/steamrtarm64/steam"

cat > "${T}/bin/pop" <<'EOF'
#!/usr/bin/bash
f="${STATE}/$1"
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
case "$*" in
    "systemctl --user is-active --quiet gamescope-session-plus@steam.service")
        [[ "$(pop steam)" == active ]] ;;
    "systemctl --user is-active --quiet gamescope-session-plus@casdap.service")
        [[ "$(cat "${STATE}/casdap")" == active ]] ;;
    "systemctl --user stop gamescope-session-plus@casdap.service")
        echo inactive > "${STATE}/casdap" ;;
    "busctl --user status org.kde.Shutdown")
        [[ "$(cat "${STATE}/plasma")" == 1 ]] ;;
    *"/steam -shutdown")
        printf '%s\n' "${vars[@]}" > "${STATE}/shutdown_env" ;;
    *qdbus*)
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
case "$(pop sessions)" in
    active)  echo '[{"session":"1","uid":969,"user":"sddm","seat":"seat0","state":"online"},{"session":"16","uid":1000,"user":"armada","seat":"seat0","state":"active"},{"session":"7","uid":1000,"user":"armada","seat":null,"state":"active"}]' ;;
    closing) echo '[{"session":"16","uid":1000,"user":"armada","seat":"seat0","state":"closing"},{"session":"7","uid":1000,"user":"armada","seat":null,"state":"active"}]' ;;
    gone)    echo '[{"session":"7","uid":1000,"user":"armada","seat":null,"state":"active"},{"session":"19","uid":1000,"user":"armada","seat":"seat0","state":"active"}]' ;;
esac
EOF

cat > "${T}/bin/sleep" <<'EOF'
#!/usr/bin/bash
echo "sleep $*" >> "${STATE}/calls"
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
echo "$*" >> "${STATE}/logger"
EOF

cat > "${T}/bin/getent" <<'EOF'
#!/usr/bin/bash
echo "armada:x:1000:1000::${HOME_DIR}:/bin/bash"
EOF

cat > "${T}/bin/qdbus" <<'EOF'
#!/usr/bin/bash
exit 0
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"
export HOME_DIR="${T}/home"
export ARMADA_SDDM_CONF_DIR="${T}/sddm"
export ARMADA_SESSION_RUNTIME_DIR="${T}/run"

failures=0
case_name=

setup() {
    case_name=$1
    rm -rf "${STATE}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/logger"
    printf '%s\n' "${2}" | tr ' ' '\n' > "${STATE}/steam"
    echo "${3}" > "${STATE}/casdap"
    echo "${4}" > "${STATE}/plasma"
    printf '%s\n' "${5}" | tr ' ' '\n' > "${STATE}/sessions"
    rm -f "${T}/sddm/zz-holo-autologin.conf"
}

run() {
    rc=0
    "${sc}" "$@" || rc=$?
}

fail() {
    echo "FAIL ${case_name}: $*" >&2
    echo "--- calls" >&2
    cat "${STATE}/calls" >&2
    failures=$((failures + 1))
}

expect_rc() { [[ "${rc}" == "$1" ]] || fail "exit ${rc}, expected $1"; }
expect_call() { grep -qF -- "$1" "${STATE}/calls" || fail "missing call: $1"; }
expect_no_call() { grep -qF -- "$1" "${STATE}/calls" && fail "unexpected call: $1" || true; }
expect_session() {
    [[ "$(cat "${T}/sddm/zz-holo-autologin.conf")" == "$(printf '[Autologin]\nSession=%s' "$1")" ]] \
        || fail "drop-in is $(tr '\n' ' ' < "${T}/sddm/zz-holo-autologin.conf"), expected $1"
}
expect_sleeps() {
    local n
    n=$(grep -c '^sleep ' "${STATE}/calls" || true)
    [[ "${n}" -ge "$1" && "${n}" -le "$2" ]] || fail "${n} sleeps, expected $1..$2"
}

sddm_restart="systemctl --no-block restart sddm.service"
steam_shutdown="/steam -shutdown"
casdap_stop="systemctl --user stop gamescope-session-plus@casdap.service"

setup "switch-casdap from game mode" "active inactive" inactive 0 "active gone"
run switch-casdap
expect_rc 0
expect_session casdap.desktop
expect_call "${steam_shutdown}"
expect_no_call "${sddm_restart}"
expect_no_call qdbus
expect_call "busctl --user status org.kde.Shutdown"
expect_no_call "${casdap_stop}"
grep -qxF "HOME=${T}/home" "${STATE}/shutdown_env" || fail "HOME not ${T}/home"
grep -qxF "XDG_RUNTIME_DIR=${T}/run" "${STATE}/shutdown_env" || fail "XDG_RUNTIME_DIR not ${T}/run"
grep -qxF "DBUS_SESSION_BUS_ADDRESS=unix:path=${T}/run/bus" "${STATE}/shutdown_env" || fail "DBUS_SESSION_BUS_ADDRESS wrong"
grep -qxF "LD_LIBRARY_PATH=${T}/home/.local/share/Steam/steamrtarm64" "${STATE}/shutdown_env" || fail "LD_LIBRARY_PATH wrong"
grep -q "^-t casdap-switch -- steam -shutdown HOME=${T}/home XDG_RUNTIME_DIR=${T}/run session=16$" "${STATE}/logger" \
    || fail "logger line missing: $(cat "${STATE}/logger")"
grep -q "^sudo HOME=" "${STATE}/calls" || fail "steam -shutdown ran without explicit HOME"
grep -qE "^sudo .*HOME=.* :: .*/steam -shutdown" "${STATE}/calls" || fail "steam -shutdown call not recorded"

setup "switch-casdap from Plasma" inactive inactive 1 active
run switch-casdap
expect_rc 0
expect_session casdap.desktop
expect_call qdbus
expect_no_call "${steam_shutdown}"
expect_no_call "${sddm_restart}"

setup "switch-gamemode from casdap" inactive active 0 active
run switch-gamemode
expect_rc 0
expect_session gamescope-session-steam.desktop
expect_call "${casdap_stop}"
expect_no_call qdbus
expect_no_call "${sddm_restart}"
[[ "$(cat "${STATE}/casdap")" == inactive ]] || fail "casdap unit still active"

setup "switch-gamemode from Plasma" inactive inactive 1 active
run switch-gamemode
expect_rc 0
expect_call qdbus
expect_no_call "${sddm_restart}"

setup "switch-gamemode with no Plasma owner" inactive inactive 0 active
run switch-gamemode
expect_rc 0
expect_call "busctl --user status org.kde.Shutdown"
expect_no_call qdbus
expect_call "${sddm_restart}"

setup "default-gamemode" active inactive 1 active
run default-gamemode
expect_rc 0
expect_session gamescope-session-steam.desktop
expect_call "systemctl reset-failed sddm.service"
expect_no_call "${sddm_restart}"
expect_no_call sudo

setup "default-casdap" active inactive 1 active
run default-casdap
expect_rc 0
expect_session casdap.desktop
expect_no_call "${sddm_restart}"
expect_no_call sudo

setup "switch-casdap with Steam unit inactive" inactive inactive 0 active
run switch-casdap
expect_rc 0
expect_no_call "${steam_shutdown}"
expect_call "${sddm_restart}"

setup "switch-casdap session closing" "active inactive" inactive 0 "active closing"
run switch-casdap
expect_rc 0
expect_call "${steam_shutdown}"
expect_no_call "${sddm_restart}"

setup "switch-casdap session still active after wait" "active inactive" inactive 0 active
run switch-casdap
expect_rc 0
expect_call "${steam_shutdown}"
expect_call "${sddm_restart}"
expect_sleeps 10 10

setup "switch-casdap Steam unit back active during wait" "active inactive active" inactive 0 active
run switch-casdap
expect_rc 0
expect_call "${steam_shutdown}"
expect_call "${sddm_restart}"
expect_sleeps 1 1

setup "switch-casdap Steam unit never stops" "active active" inactive 0 active
run switch-casdap
expect_rc 0
expect_call "${steam_shutdown}"
expect_call "${sddm_restart}"
expect_sleeps 20 20

setup "unknown verb" inactive inactive 0 active
run bogus 2>/dev/null
expect_rc 1
[[ -e "${T}/sddm/zz-holo-autologin.conf" ]] && fail "drop-in written for unknown verb" || true

if (( failures )); then
    echo "session-control-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "session-control-test: ok"
