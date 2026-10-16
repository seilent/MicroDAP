#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
stopped="${repo}/data/libexec/microdap-stopped"
dropin="${repo}/data/systemd/user/gamescope-session-plus@microdap.service.d/50-microdap.conf"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export LOG="${T}/log" CALLS="${T}/calls" NMCLI_RC="${T}/nmcli-rc"
export MICRODAP_SYS_ROOT="${T}/sys"
export HOME="${T}/home"
marker="${HOME}/.local/state/microdap/wifi-off"

cat > "${T}/bin/logger" <<'STUB'
#!/usr/bin/bash
echo "$2: ${*:4}" >> "${LOG}"
STUB
cat > "${T}/bin/nmcli" <<'STUB'
#!/usr/bin/bash
echo "$*" >> "${CALLS}"
exit "$(cat "${NMCLI_RC}" 2>/dev/null || echo 0)"
STUB
chmod 0755 "${T}/bin/logger" "${T}/bin/nmcli"
export PATH="${T}/bin:${PATH}"

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; failures=$((failures + 1)); }

begin() {
    case_name=$1
    rm -rf "${MICRODAP_SYS_ROOT}" "${HOME}" "${LOG}" "${CALLS}" "${NMCLI_RC}"
    mkdir -p "${HOME}"
    : > "${LOG}"
    : > "${CALLS}"
}

panel() {
    mkdir -p "${MICRODAP_SYS_ROOT}/class/backlight/$1"
    printf '%s\n' "$2" > "${MICRODAP_SYS_ROOT}/class/backlight/$1/bl_power"
}

run() {
    rc=0
    "${stopped}" || rc=$?
    [[ "${rc}" == 0 ]] || fail "exit ${rc}"
}

expect_power() {
    [[ "$(cat "${MICRODAP_SYS_ROOT}/class/backlight/$1/bl_power")" == "$2" ]] || fail "$1 bl_power $(cat "${MICRODAP_SYS_ROOT}/class/backlight/$1/bl_power"), expected $2"
}

begin panel-off
panel panel0 4
panel panel1 4
run
expect_power panel0 0
expect_power panel1 0
[[ ! -s "${CALLS}" ]] || fail "nmcli called without a marker: $(cat "${CALLS}")"

begin no-backlight
run
[[ ! -s "${CALLS}" ]] || fail "nmcli called without a marker"

begin marker
panel panel0 0
mkdir -p "$(dirname "${marker}")"
: > "${marker}"
run
expect_power panel0 0
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")'"
[[ ! -e "${marker}" ]] || fail "marker left"
grep -qxF 'microdap-stopped: wifi radio on' "${LOG}" || fail "log $(cat "${LOG}")"

begin nmcli-fails
mkdir -p "$(dirname "${marker}")"
: > "${marker}"
echo 1 > "${NMCLI_RC}"
run
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")'"
[[ -e "${marker}" ]] || fail "marker removed after a failed radio on"
grep -qxF 'microdap-stopped: wifi radio on failed, marker kept' "${LOG}" || fail "log $(cat "${LOG}")"

begin unwritable
mkdir -p "${MICRODAP_SYS_ROOT}/class/backlight/panel0/bl_power"
panel panel1 4
run
expect_power panel1 0
grep -qxF "microdap-stopped: ${MICRODAP_SYS_ROOT}/class/backlight/panel0/bl_power write failed" "${LOG}" || fail "log $(cat "${LOG}")"

case_name=files
[[ -x "${stopped}" ]] || fail "microdap-stopped is not executable"
[[ "$(cat "${dropin}")" == $'[Service]\nExecStopPost=-/usr/libexec/microdap/microdap-stopped' ]] || fail "drop in content"
for f in "${stopped}" "${dropin}"; do
    grep -v '^#!' "${f}" | grep -qE '^\s*#' && fail "${f} has a comment"
    LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}" && fail "${f} has a dash"
done

if (( failures )); then
    echo "stopped-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "stopped-test: ok"
