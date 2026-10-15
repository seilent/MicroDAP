#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
guard="${repo}/sysext/usr/libexec/microdap/microdap-guard"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin" "${T}/share"
export STATE="${T}/state"
export MICRODAP_SHARE="${T}/share"
export MICRODAP_RUN="${T}/run"
export MICRODAP_SESSION_CONTROL="${T}/bin/session-control"

cat > "${T}/bin/session-control" <<'EOF'
#!/usr/bin/bash
echo "session-control $*" >> "${STATE}/calls"
case "$(cat "${STATE}/mode")" in
    listed)  printf 'microdap\nmicroemu\n' ;;
    other)   echo microemu ;;
    usage)   echo "usage: session-control {switch-desktop|switch-gamemode}" >&2; exit 1 ;;
esac
EOF

cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
echo "$*" >> "${STATE}/logger"
EOF

chmod 0755 "${T}"/bin/*
export PATH="${T}/bin:${PATH}"

failures=0
case_name=

setup() {
    case_name=$1
    rm -rf "${STATE}" "${MICRODAP_RUN}"
    mkdir -p "${STATE}"
    : > "${STATE}/calls"
    : > "${STATE}/logger"
    echo "$2" > "${STATE}/mode"
}

run() {
    rc=0
    "${guard}" > /dev/null 2>&1 || rc=$?
}

fail() { echo "FAIL ${case_name}: $*" >&2; failures=$((failures + 1)); }

expect_drift() {
    [[ "$(cat "${MICRODAP_RUN}/drift" 2>/dev/null)" == "${MICRODAP_SESSION_CONTROL}" ]] \
        || fail "drift is '$(cat "${MICRODAP_RUN}/drift" 2>/dev/null)'"
    grep -q "^-t microdap-guard -p user.warning -- session-control does not list the microdap session$" "${STATE}/logger" \
        || fail "no warning: $(cat "${STATE}/logger")"
}

setup "microdap listed" listed
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
grep -qxF "session-control list-sessions" "${STATE}/calls" || fail "list-sessions not called"
[[ -e "${MICRODAP_RUN}/drift" ]] && fail "drift written: $(cat "${MICRODAP_RUN}/drift")"
[[ -s "${STATE}/logger" ]] && fail "logged: $(cat "${STATE}/logger")"

setup "session-control without drop-ins" usage
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
expect_drift

setup "microdap not listed" other
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
expect_drift

if (( failures )); then
    echo "guard-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "guard-test: ok"
