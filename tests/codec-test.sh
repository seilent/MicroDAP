#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
codec="${repo}/sysext/usr/libexec/microdap/microdap-codec"
dropin="${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@microdap.service.d/10-microdap-bt.conf"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
log="${T}/calls.log"
cat > "${T}/bin/systemctl" <<EOF
#!/usr/bin/bash
printf 'systemctl %s\n' "\$*" >> "${log}"
exit \${MICRODAP_STUB_RC:-0}
EOF
cat > "${T}/bin/logger" <<'EOF'
#!/usr/bin/bash
exit 0
EOF
chmod +x "${T}/bin/systemctl" "${T}/bin/logger"
cat > "${T}/microdap-keep" <<EOF
#!/usr/bin/bash
exec bash "${repo}/sysext/usr/libexec/microdap/microdap-keep" "\$@"
EOF
chmod +x "${T}/microdap-keep"
export PATH="${T}/bin:${PATH}"
export MICRODAP_WP_CONF_DIR="${T}/wp"
export MICRODAP_KEEP_BIN="${T}/microdap-keep"
export MICRODAP_KEEP_FILE="${T}/keep"
export MICRODAP_BOOT_ID_FILE="${T}/boot_id"
boot_id=0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10
printf '%s\n' "${boot_id}" > "${MICRODAP_BOOT_ID_FILE}"
fragment="${MICRODAP_WP_CONF_DIR}/50-microdap-codec.conf"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

run() {
    rc=0
    : > "${log}"
    out=$(bash "${codec}" "$@" 2>&1) || rc=$?
}

write_fragment() {
    mkdir -p "${MICRODAP_WP_CONF_DIR}"
    printf 'monitor.bluez.properties = {\n    bluez5.codecs = [ sbc aac ]\n}\n' > "${fragment}"
}

rm -f "${MICRODAP_KEEP_FILE}"
write_fragment
run clear
[[ "${rc}" == 0 ]] || fail "clear exit ${rc}"
[[ ! -e "${fragment}" ]] || fail "clear left the fragment"
[[ "$(cat "${log}")" == "systemctl --user restart wireplumber" ]] || fail "clear calls '$(cat "${log}")'"

run clear
[[ "${rc}" == 0 ]] || fail "clear with no file exit ${rc}"
[[ ! -s "${log}" ]] || fail "clear with no file called '$(cat "${log}")'"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
write_fragment
run clear
[[ "${rc}" == 0 ]] || fail "clear with the keep marker exit ${rc}"
[[ -e "${fragment}" ]] || fail "clear with the keep marker removed the fragment"
[[ ! -s "${log}" ]] || fail "clear with the keep marker called '$(cat "${log}")'"

printf '00000000-0000-0000-0000-000000000000\n' > "${MICRODAP_KEEP_FILE}"
write_fragment
run clear
[[ ! -e "${fragment}" ]] || fail "stale marker left the fragment"
[[ "$(cat "${log}")" == "systemctl --user restart wireplumber" ]] || fail "stale marker calls '$(cat "${log}")'"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
write_fragment
export MICRODAP_KEEP_BIN="${T}/missing/microdap-keep"
run clear
export MICRODAP_KEEP_BIN="${T}/microdap-keep"
[[ ! -e "${fragment}" ]] || fail "missing microdap-keep left the fragment"
[[ "$(cat "${log}")" == "systemctl --user restart wireplumber" ]] || fail "missing microdap-keep calls '$(cat "${log}")'"

rm -f "${MICRODAP_KEEP_FILE}"
write_fragment
export MICRODAP_STUB_RC=1
run clear
unset MICRODAP_STUB_RC
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

grep -qx 'ExecStopPost=-/usr/libexec/microdap/microdap-codec clear' "${dropin}" || fail "drop in lacks the ExecStopPost line"
grep -qx 'Wants=microdap-bt.service' "${dropin}" || fail "drop in lacks Wants=microdap-bt.service"

if (( failures )); then
    echo "codec-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "codec-test: ok"
