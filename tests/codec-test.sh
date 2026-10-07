#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
codec="${repo}/sysext/usr/libexec/casdap/casdap-codec"
dropin="${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@casdap.service.d/10-casdap-bt.conf"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
log="${T}/calls.log"
cat > "${T}/bin/systemctl" <<EOF
#!/usr/bin/bash
printf 'systemctl %s\n' "\$*" >> "${log}"
exit \${CASDAP_STUB_RC:-0}
EOF
cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
exit 0
EOF
chmod +x "${T}/bin/systemctl" "${T}/bin/logger"
export PATH="${T}/bin:${PATH}"
export CASDAP_WP_CONF_DIR="${T}/wp"
export CASDAP_SETTINGS_FILE="${T}/settings.ini"
fragment="${CASDAP_WP_CONF_DIR}/50-casdap-codec.conf"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

run() {
    rc=0
    : > "${log}"
    out=$(bash "${codec}" "$@" 2>&1) || rc=$?
}

write_fragment() {
    mkdir -p "${CASDAP_WP_CONF_DIR}"
    printf 'monitor.bluez.properties = {\n    bluez5.codecs = [ sbc aac ]\n}\n' > "${fragment}"
}

rm -f "${CASDAP_SETTINGS_FILE}"
write_fragment
run clear
[[ "${rc}" == 0 ]] || fail "clear exit ${rc}"
[[ ! -e "${fragment}" ]] || fail "clear left the fragment"
[[ "$(cat "${log}")" == "systemctl --user restart wireplumber" ]] || fail "clear calls '$(cat "${log}")'"

run clear
[[ "${rc}" == 0 ]] || fail "clear with no file exit ${rc}"
[[ ! -s "${log}" ]] || fail "clear with no file called '$(cat "${log}")'"

printf '[General]\nkeepPhoneInGameMode=true\n' > "${CASDAP_SETTINGS_FILE}"
write_fragment
run clear
[[ "${rc}" == 0 ]] || fail "clear with the phone kept exit ${rc}"
[[ ! -e "${fragment}" ]] || fail "clear with the phone kept left the fragment"
[[ ! -s "${log}" ]] || fail "clear with the phone kept called '$(cat "${log}")'"

printf '[General]\r\nkeepPhoneInGameMode=false\r\n' > "${CASDAP_SETTINGS_FILE}"
write_fragment
run clear
[[ "$(cat "${log}")" == "systemctl --user restart wireplumber" ]] || fail "keep false calls '$(cat "${log}")'"

rm -f "${CASDAP_SETTINGS_FILE}"
write_fragment
export CASDAP_STUB_RC=1
run clear
unset CASDAP_STUB_RC
[[ "${rc}" == 1 ]] || fail "failed restart exit ${rc}, expected 1"
[[ ! -e "${fragment}" ]] || fail "failed restart left the fragment"

for verb in bogus "" "clear --help" write; do
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
done
run
[[ "${rc}" == 1 ]] || fail "no verb exit ${rc}, expected 1"
run clear clear
[[ "${rc}" == 1 ]] || fail "two args exit ${rc}, expected 1"

grep -qx 'ExecStopPost=/usr/libexec/casdap/casdap-codec clear' "${dropin}" || fail "drop in lacks the ExecStopPost line"
grep -qx 'Wants=casdap-bt.service' "${dropin}" || fail "drop in lacks Wants=casdap-bt.service"

if (( failures )); then
    echo "codec-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "codec-test: ok"
