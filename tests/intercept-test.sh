#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
intercept="${repo}/sysext/usr/libexec/casdap/casdap-intercept"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin" "${T}/modes"
export STATE="${T}/state"
export TREE="${T}/tree"
export MODES="${T}/modes"

cat > "${T}/bin/busctl" <<'EOF'
#!/usr/bin/bash
echo "busctl $*" >> "${STATE}"
case "${2:-}" in
    tree) cat "${TREE}" ;;
    get-property) cat "${MODES}/$(basename "$4")" ;;
    set-property) [[ -f "${MODES}/fail" ]] && exit 1 || true ;;
esac
EOF
cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
echo "logger $*" >> "${STATE}"
EOF
chmod 0755 "${T}/bin/busctl" "${T}/bin/logger"
export PATH="${T}/bin:${PATH}"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

reset() {
    : > "${STATE}"
    rm -f "${MODES}"/*
    cat > "${TREE}" <<'EOF'
/org/shadowblip/InputPlumber
/org/shadowblip/InputPlumber/CompositeDevice0
/org/shadowblip/InputPlumber/CompositeDevice1
/org/shadowblip/InputPlumber/devices/source/event3
/org/shadowblip/InputPlumber/devices/target/dbus0
/org/shadowblip/InputPlumber/devices/target/gamepad0
/org/shadowblip/InputPlumber/devices/target/keyboard0
EOF
}

modes() {
    echo "u $1" > "${MODES}/CompositeDevice0"
    echo "u $2" > "${MODES}/CompositeDevice1"
}

run() {
    rc=0
    out=$("${intercept}" "$@") || rc=$?
}

count() {
    grep -c -- "$1" "${STATE}" || true
}

set_call="busctl --system set-property org.shadowblip.InputPlumber"
iface="org.shadowblip.Input.CompositeDevice InterceptMode u"
dev0=/org/shadowblip/InputPlumber/CompositeDevice0
dev1=/org/shadowblip/InputPlumber/CompositeDevice1

for mode in 0 3; do
    reset
    run "${mode}"
    [[ "${rc}" == 0 ]] || fail "${mode} exit ${rc}"
    [[ -z "${out}" ]] || fail "${mode} printed '${out}'"
    [[ "$(count "${set_call} ${dev0} ${iface} ${mode}$")" == 1 ]] || fail "${mode}: CompositeDevice0 not set: $(cat "${STATE}")"
    [[ "$(count "${set_call} ${dev1} ${iface} ${mode}$")" == 1 ]] || fail "${mode}: CompositeDevice1 not set: $(cat "${STATE}")"
    [[ "$(count "set-property")" == 2 ]] || fail "${mode}: expected 2 set-property calls: $(cat "${STATE}")"
    [[ "$(count "devices/")" == 0 ]] || fail "${mode}: touched a non composite path: $(cat "${STATE}")"
    [[ "$(count "get-property")" == 0 ]] || fail "${mode}: read a property: $(cat "${STATE}")"
done

reset
touch "${MODES}/fail"
run 0
[[ "${rc}" == 0 ]] || fail "0 with failing set-property exit ${rc}"
[[ "$(count "set-property")" == 2 ]] || fail "0 with failing set-property stopped early: $(cat "${STATE}")"
[[ "$(count "^logger -t casdap-intercept set ${dev0} InterceptMode 0 failed")" == 1 ]] || fail "0 failure not logged: $(cat "${STATE}")"

reset
modes 0 3
run get
[[ "${rc}" == 0 ]] || fail "get exit ${rc}"
[[ "${out}" == 3 ]] || fail "get with 0 3 printed '${out}'"
[[ "$(count "set-property")" == 0 ]] || fail "get wrote a property: $(cat "${STATE}")"

reset
modes 3 0
run get
[[ "${out}" == 3 ]] || fail "get with 3 0 printed '${out}'"
[[ "$(count "get-property")" == 1 ]] || fail "get with 3 0 read past the first hit: $(cat "${STATE}")"

reset
modes 0 0
run get
[[ "${rc}" == 0 ]] || fail "get with 0 0 exit ${rc}"
[[ "${out}" == 0 ]] || fail "get with 0 0 printed '${out}'"
[[ "$(count "get-property")" == 2 ]] || fail "get with 0 0 expected 2 reads: $(cat "${STATE}")"

reset
modes 2 0
run get
[[ "${out}" == 0 ]] || fail "get with 2 0 printed '${out}'"

reset
printf '/org/shadowblip/InputPlumber\n/org/shadowblip/InputPlumber/devices/target/gamepad0\n' > "${TREE}"
run 3
[[ "${rc}" == 0 ]] || fail "3 with no device exit ${rc}"
[[ "$(count "set-property")" == 0 ]] || fail "3 with no device wrote: $(cat "${STATE}")"
run get
[[ "${rc}" == 0 ]] || fail "get with no device exit ${rc}"
[[ "${out}" == 0 ]] || fail "get with no device printed '${out}'"

for verb in bogus "" 1 2 "0;id" "get --help" 00; do
    reset
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
    [[ -s "${STATE}" ]] && fail "verb '${verb}' ran $(cat "${STATE}")" || true
done

reset
run
[[ "${rc}" == 1 ]] || fail "no verb exit ${rc}, expected 1"
[[ -s "${STATE}" ]] && fail "no verb ran $(cat "${STATE}")" || true

reset
run 0 3
[[ "${rc}" == 1 ]] || fail "two args exit ${rc}, expected 1"
[[ -s "${STATE}" ]] && fail "two args ran $(cat "${STATE}")" || true

if (( failures )); then
    echo "intercept-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "intercept-test: ok"
