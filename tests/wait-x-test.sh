#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
waitx="${repo}/sysext/usr/libexec/microdap/microdap-wait-x"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin" "${T}/sock"
export PATH="${T}/bin:${PATH}"
export MICRODAP_X_SOCKET_DIR="${T}/sock"
export DISPLAY=:7
calls="${T}/xprop.calls"
fail_n="${T}/xprop.fail"

cat > "${T}/bin/xprop" <<'EOF'
#!/usr/bin/bash
printf '%s\n' "$*" >> "${XPROP_CALLS}"
n=$(wc -l < "${XPROP_CALLS}")
limit=$(cat "${XPROP_FAIL_N}")
(( n > limit ))
EOF
chmod +x "${T}/bin/xprop"
export XPROP_CALLS="${calls}" XPROP_FAIL_N="${fail_n}"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

now_ms() { printf '%s\n' "$(( $(date +%s%N) / 1000000 ))"; }

run() {
    rc=0
    start=$(now_ms)
    bash "${waitx}" > /dev/null 2>&1 || rc=$?
    elapsed=$(( $(now_ms) - start ))
}

: > "${calls}"
printf '2\n' > "${fail_n}"
rm -f "${T}/sock/X7"
( sleep 0.5; : > "${T}/sock/X7" ) &
MICRODAP_WAIT_X_MS=5000 run
wait
[[ "${rc}" == 0 ]] || fail "socket appearing later exit ${rc}"
(( elapsed >= 400 )) || fail "returned after ${elapsed} ms, expected a wait"
(( $(wc -l < "${calls}") == 3 )) || fail "xprop called $(wc -l < "${calls}") times, expected 3"
grep -q -x -- '-root -display :7' "${calls}" || fail "xprop args: $(head -1 "${calls}")"

: > "${calls}"
printf '0\n' > "${fail_n}"
: > "${T}/sock/X7"
MICRODAP_WAIT_X_MS=5000 run
[[ "${rc}" == 0 ]] || fail "socket present exit ${rc}"
(( elapsed < 400 )) || fail "socket present took ${elapsed} ms"
(( $(wc -l < "${calls}") == 1 )) || fail "xprop called $(wc -l < "${calls}") times, expected 1"

: > "${calls}"
printf '0\n' > "${fail_n}"
rm -f "${T}/sock/X7"
MICRODAP_WAIT_X_MS=600 run
[[ "${rc}" == 1 ]] || fail "no socket exit ${rc}, expected 1"
(( elapsed >= 500 )) || fail "timeout returned after ${elapsed} ms"
(( $(wc -l < "${calls}") == 0 )) || fail "xprop called without a socket"

: > "${calls}"
printf '1000\n' > "${fail_n}"
: > "${T}/sock/X7"
MICRODAP_WAIT_X_MS=600 run
[[ "${rc}" == 1 ]] || fail "socket without server exit ${rc}, expected 1"
(( $(wc -l < "${calls}") >= 3 )) || fail "xprop retried $(wc -l < "${calls}") times, expected at least 3"

: > "${calls}"
printf '0\n' > "${fail_n}"
: > "${T}/sock/X0"
rc=0
(unset DISPLAY; MICRODAP_WAIT_X_MS=600 bash "${waitx}" > /dev/null 2>&1) || rc=$?
[[ "${rc}" == 0 ]] || fail "default display exit ${rc}"
grep -q -x -- '-root -display :0' "${calls}" || fail "default display args: $(head -1 "${calls}")"

if (( failures )); then
    echo "wait-x-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "wait-x-test: ok"
