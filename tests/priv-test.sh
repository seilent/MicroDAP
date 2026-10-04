#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
priv="${repo}/sysext/usr/libexec/casdap/casdap-priv"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin" "${T}/sys/class/backlight/panel0" "${T}/sys/class/backlight/panel1"
export STATE="${T}/state"
export CASDAP_SYSFS="${T}/sys"

cat > "${T}/bin/btmgmt" <<'EOF'
#!/usr/bin/bash
echo "btmgmt $*" >> "${STATE}"
EOF
chmod 0755 "${T}/bin/btmgmt"
export PATH="${T}/bin:${PATH}"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

reset() {
    : > "${STATE}"
    echo 0 > "${T}/sys/class/backlight/panel0/bl_power"
    echo 0 > "${T}/sys/class/backlight/panel1/bl_power"
}

run() {
    rc=0
    "${priv}" "$@" || rc=$?
}

reset
run bt-class-headphones
[[ "${rc}" == 0 ]] || fail "bt-class-headphones exit ${rc}"
[[ "$(cat "${STATE}")" == "btmgmt class 4 24" ]] || fail "bt-class-headphones ran $(cat "${STATE}")"

reset
run bt-class-default
[[ "${rc}" == 0 ]] || fail "bt-class-default exit ${rc}"
[[ "$(cat "${STATE}")" == "btmgmt class 0 0" ]] || fail "bt-class-default ran $(cat "${STATE}")"

reset
run backlight-off
[[ "${rc}" == 0 ]] || fail "backlight-off exit ${rc}"
[[ "$(cat "${T}/sys/class/backlight/panel0/bl_power")" == 4 ]] || fail "panel0 not off"
[[ "$(cat "${T}/sys/class/backlight/panel1/bl_power")" == 4 ]] || fail "panel1 not off"
[[ -s "${STATE}" ]] && fail "backlight-off called btmgmt" || true

reset
echo 4 > "${T}/sys/class/backlight/panel0/bl_power"
run backlight-on
[[ "${rc}" == 0 ]] || fail "backlight-on exit ${rc}"
[[ "$(cat "${T}/sys/class/backlight/panel0/bl_power")" == 0 ]] || fail "panel0 not on"

reset
chmod 0444 "${T}/sys/class/backlight/panel0/bl_power"
run backlight-off
[[ "${rc}" == 0 ]] || fail "backlight-off with a read only node exit ${rc}"
[[ "$(cat "${T}/sys/class/backlight/panel1/bl_power")" == 4 ]] || fail "panel1 skipped after a read only node"
chmod 0644 "${T}/sys/class/backlight/panel0/bl_power"

for verb in bogus "" "cores-offline;id" "bt-class-headphones;id" "backlight-off --help"; do
    reset
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
    [[ -s "${STATE}" ]] && fail "verb '${verb}' ran $(cat "${STATE}")" || true
    [[ "$(cat "${T}/sys/class/backlight/panel0/bl_power")" == 0 ]] || fail "verb '${verb}' touched sysfs"
done

reset
run
[[ "${rc}" == 1 ]] || fail "no verb exit ${rc}, expected 1"

if (( failures )); then
    echo "priv-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "priv-test: ok"
