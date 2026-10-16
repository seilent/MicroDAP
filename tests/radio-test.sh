#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
radio="${repo}/data/libexec/microdap-radio"
unit="${repo}/data/systemd/system/microdap-radio.service"
preset="${repo}/data/systemd/system-preset/70-microdap.preset"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export LOG="${T}/log" CALLS="${T}/calls" NMCLI_RC="${T}/nmcli-rc"
export MICRODAP_HOME_ROOT="${T}/home"

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
    rm -rf "${MICRODAP_HOME_ROOT}" "${T:?}/var" "${T:?}/outside" "${LOG}" "${CALLS}" "${NMCLI_RC}"
    mkdir -p "${MICRODAP_HOME_ROOT}"
    : > "${LOG}"
    : > "${CALLS}"
}

marker_of() { printf '%s/%s/.local/state/microdap/wifi-off' "${MICRODAP_HOME_ROOT}" "$1"; }

mark() {
    mkdir -p "$(dirname "$(marker_of "$1")")"
    : > "$(marker_of "$1")"
}

run() {
    rc=0
    "${radio}" || rc=$?
}

begin no-marker
mkdir -p "${MICRODAP_HOME_ROOT}/micro/.local/state/microdap"
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ ! -s "${CALLS}" ]] || fail "nmcli called: $(cat "${CALLS}")"

begin no-homes
rmdir "${MICRODAP_HOME_ROOT}"
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ ! -s "${CALLS}" ]] || fail "nmcli called"

begin marker
mark micro
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")'"
[[ ! -e "$(marker_of micro)" ]] || fail "marker left"
grep -qxF 'microdap-radio: wifi radio on' "${LOG}" || fail "log $(cat "${LOG}")"

begin two-homes
mark micro
mark guest
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")', expected one"
[[ ! -e "$(marker_of micro)" && ! -e "$(marker_of guest)" ]] || fail "marker left"

begin home-symlink
mkdir -p "${T}/var/home/micro/.local/state/microdap"
: > "${T}/var/home/micro/.local/state/microdap/wifi-off"
ln -s "${T}/var/home/micro" "${MICRODAP_HOME_ROOT}/micro"
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")'"
[[ ! -e "${T}/var/home/micro/.local/state/microdap/wifi-off" ]] || fail "marker under the resolved home left"

begin marker-symlink
mkdir -p "${T}/outside" "$(dirname "$(marker_of micro)")"
: > "${T}/outside/keep"
ln -s "${T}/outside/keep" "$(marker_of micro)"
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ "$(cat "${CALLS}")" == "radio wifi on" ]] || fail "nmcli calls '$(cat "${CALLS}")'"
[[ -e "${T}/outside/keep" ]] || fail "symlink target removed"
[[ -L "$(marker_of micro)" ]] || fail "symlink marker removed"
grep -qxF "microdap-radio: $(marker_of micro) is not a plain file, left in place" "${LOG}" || fail "log $(cat "${LOG}")"

begin dir-symlink
mkdir -p "${T}/outside" "${MICRODAP_HOME_ROOT}/micro/.local/state"
: > "${T}/outside/wifi-off"
ln -s "${T}/outside" "${MICRODAP_HOME_ROOT}/micro/.local/state/microdap"
run
[[ "${rc}" == 0 ]] || fail "exit ${rc}"
[[ -e "${T}/outside/wifi-off" ]] || fail "file behind a linked dir removed"

begin nmcli-fails
mark micro
echo 1 > "${NMCLI_RC}"
run
[[ "${rc}" == 1 ]] || fail "exit ${rc}, expected 1"
[[ -e "$(marker_of micro)" ]] || fail "marker removed after a failed radio on"
grep -qxF 'microdap-radio: wifi radio on failed' "${LOG}" || fail "log $(cat "${LOG}")"

case_name=files
[[ -x "${radio}" ]] || fail "microdap-radio is not executable"
expected=$'[Unit]\nDescription=MicroDAP Wi-Fi radio restore\nAfter=NetworkManager.service\nWants=NetworkManager.service\n\n[Service]\nType=oneshot\nExecStart=/usr/libexec/microdap/microdap-radio\n\n[Install]\nWantedBy=multi-user.target'
[[ "$(cat "${unit}")" == "${expected}" ]] || fail "unit content"
grep -qxF 'enable microdap-radio.service' "${preset}" || fail "preset does not enable the unit"
for f in "${radio}" "${unit}"; do
    grep -v '^#!' "${f}" | grep -qE '^\s*#' && fail "${f} has a comment"
    LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}" && fail "${f} has a dash"
done

if (( failures )); then
    echo "radio-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "radio-test: ok"
