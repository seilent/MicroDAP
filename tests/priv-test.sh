#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
priv="${repo}/sysext/usr/libexec/casdap/casdap-priv"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
sys="${T}/sys"
cpu="${sys}/devices/system/cpu"
gpu="${sys}/class/devfreq/3d00000.gpu"
mkdir -p "${T}/bin" "${sys}/class/backlight/panel0" "${sys}/class/backlight/panel1" \
    "${cpu}/cpufreq/policy0" "${cpu}/cpufreq/policy4" "${cpu}/cpufreq/policy7" "${gpu}"
for n in 1 2 3 4 5 6 7; do
    mkdir -p "${cpu}/cpu${n}"
done
export STATE="${T}/state"
export CASDAP_SYSFS_ROOT="${sys}"
export CASDAP_CPU_BEFORE_FILE="${T}/run/cpu-before"

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
    echo 0 > "${sys}/class/backlight/panel0/bl_power"
    echo 0 > "${sys}/class/backlight/panel1/bl_power"
    echo 1804800 > "${cpu}/cpufreq/policy0/scaling_max_freq"
    echo 2419200 > "${cpu}/cpufreq/policy4/scaling_max_freq"
    echo 2841600 > "${cpu}/cpufreq/policy7/scaling_max_freq"
    for n in 1 2 3 4 5 6 7; do
        echo 1 > "${cpu}/cpu${n}/online"
    done
    echo userspace > "${gpu}/governor"
    echo 587000000 > "${gpu}/max_freq"
    rm -rf "${T}/run"
}

run() {
    rc=0
    "${priv}" "$@" 2>/dev/null || rc=$?
}

value() { cat "$1"; }

expect_value() { [[ "$(value "$1")" == "$2" ]] || fail "${case_name}: $1 is $(value "$1"), expected $2"; }

expect_online() {
    local n
    for n in 1 2 3 4 5 6 7; do
        expect_value "${cpu}/cpu${n}/online" "${!n}"
    done
}

expect_untouched_sysfs() {
    expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 1804800
    expect_value "${cpu}/cpufreq/policy4/scaling_max_freq" 2419200
    expect_value "${cpu}/cpufreq/policy7/scaling_max_freq" 2841600
    expect_online 1 1 1 1 1 1 1
    expect_value "${gpu}/governor" userspace
    expect_value "${gpu}/max_freq" 587000000
}

expect_saved() {
    [[ -f "${CASDAP_CPU_BEFORE_FILE}" ]] || { fail "${case_name}: no save file"; return 0; }
    [[ ! -e "${CASDAP_CPU_BEFORE_FILE}.tmp" ]] || fail "${case_name}: temp file left"
    [[ "$(grep -c . "${CASDAP_CPU_BEFORE_FILE}")" == 12 ]] || fail "${case_name}: $(grep -c . "${CASDAP_CPU_BEFORE_FILE}") saved lines, expected 12"
    grep -qxF "${cpu}/cpufreq/policy0/scaling_max_freq 1804800" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: policy0 not saved"
    grep -qxF "${cpu}/cpufreq/policy4/scaling_max_freq 2419200" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: policy4 not saved"
    grep -qxF "${cpu}/cpufreq/policy7/scaling_max_freq 2841600" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: policy7 not saved"
    grep -qxF "${cpu}/cpu4/online 1" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: cpu4 online not saved"
    grep -qxF "${gpu}/governor userspace" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: governor not saved"
    grep -qxF "${gpu}/max_freq 587000000" "${CASDAP_CPU_BEFORE_FILE}" || fail "${case_name}: max_freq not saved"
}

expect_dap_floor() {
    expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 787200
    expect_value "${cpu}/cpufreq/policy4/scaling_max_freq" 2419200
    expect_value "${cpu}/cpufreq/policy7/scaling_max_freq" 2841600
    expect_online 1 1 1 0 0 0 0
    expect_value "${gpu}/governor" simple_ondemand
    expect_value "${gpu}/max_freq" 305000000
}

expect_off_floor() {
    expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 518400
    expect_online 0 0 0 0 0 0 0
}

case_name=bt-class-headphones
reset
run bt-class-headphones
[[ "${rc}" == 0 ]] || fail "bt-class-headphones exit ${rc}"
[[ "$(cat "${STATE}")" == "btmgmt class 4 24" ]] || fail "bt-class-headphones ran $(cat "${STATE}")"

case_name=bt-class-default
reset
run bt-class-default
[[ "${rc}" == 0 ]] || fail "bt-class-default exit ${rc}"
[[ "$(cat "${STATE}")" == "btmgmt class 0 0" ]] || fail "bt-class-default ran $(cat "${STATE}")"

case_name=backlight-off
reset
run backlight-off
[[ "${rc}" == 0 ]] || fail "backlight-off exit ${rc}"
[[ "$(cat "${sys}/class/backlight/panel0/bl_power")" == 4 ]] || fail "panel0 not off"
[[ "$(cat "${sys}/class/backlight/panel1/bl_power")" == 4 ]] || fail "panel1 not off"
[[ -s "${STATE}" ]] && fail "backlight-off called btmgmt" || true
expect_untouched_sysfs

case_name=backlight-on
reset
echo 4 > "${sys}/class/backlight/panel0/bl_power"
run backlight-on
[[ "${rc}" == 0 ]] || fail "backlight-on exit ${rc}"
[[ "$(cat "${sys}/class/backlight/panel0/bl_power")" == 0 ]] || fail "panel0 not on"

case_name="backlight-off read only"
reset
chmod 0444 "${sys}/class/backlight/panel0/bl_power"
run backlight-off
[[ "${rc}" == 0 ]] || fail "backlight-off with a read only node exit ${rc}"
[[ "$(cat "${sys}/class/backlight/panel1/bl_power")" == 4 ]] || fail "panel1 skipped after a read only node"
chmod 0644 "${sys}/class/backlight/panel0/bl_power"

case_name=cpu-dap
reset
run cpu-dap
[[ "${rc}" == 0 ]] || fail "cpu-dap exit ${rc}"
expect_saved
expect_dap_floor
[[ -s "${STATE}" ]] && fail "cpu-dap called btmgmt" || true
expect_value "${sys}/class/backlight/panel0/bl_power" 0

case_name="cpu-dap twice"
saved=$(cat "${CASDAP_CPU_BEFORE_FILE}")
run cpu-dap
[[ "${rc}" == 0 ]] || fail "second cpu-dap exit ${rc}"
[[ "$(cat "${CASDAP_CPU_BEFORE_FILE}")" == "${saved}" ]] || fail "second cpu-dap rewrote the save file"
expect_dap_floor

case_name="cpu-off after cpu-dap"
run cpu-off
[[ "${rc}" == 0 ]] || fail "cpu-off exit ${rc}"
[[ "$(cat "${CASDAP_CPU_BEFORE_FILE}")" == "${saved}" ]] || fail "cpu-off rewrote the save file"
expect_off_floor
expect_value "${gpu}/governor" simple_ondemand
expect_value "${gpu}/max_freq" 305000000

case_name="cpu-dap after cpu-off"
run cpu-dap
[[ "${rc}" == 0 ]] || fail "cpu-dap after cpu-off exit ${rc}"
[[ "$(cat "${CASDAP_CPU_BEFORE_FILE}")" == "${saved}" ]] || fail "cpu-dap after cpu-off rewrote the save file"
expect_dap_floor

case_name=cpu-restore
run cpu-restore
[[ "${rc}" == 0 ]] || fail "cpu-restore exit ${rc}"
expect_untouched_sysfs
[[ ! -e "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "cpu-restore left the save file"

case_name="cpu-off saves first"
reset
run cpu-off
[[ "${rc}" == 0 ]] || fail "cpu-off exit ${rc}"
expect_saved
expect_off_floor
expect_value "${gpu}/governor" userspace
expect_value "${gpu}/max_freq" 587000000

case_name="cpu-off twice"
saved=$(cat "${CASDAP_CPU_BEFORE_FILE}")
run cpu-off
[[ "${rc}" == 0 ]] || fail "second cpu-off exit ${rc}"
[[ "$(cat "${CASDAP_CPU_BEFORE_FILE}")" == "${saved}" ]] || fail "second cpu-off rewrote the save file"
expect_off_floor

case_name="cpu-restore after cpu-off"
run cpu-restore
[[ "${rc}" == 0 ]] || fail "cpu-restore exit ${rc}"
expect_untouched_sysfs
[[ ! -e "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "cpu-restore left the save file"

case_name="cpu-restore without a file"
reset
echo 0 > "${cpu}/cpu2/online"
echo 0 > "${cpu}/cpu7/online"
echo 518400 > "${cpu}/cpufreq/policy0/scaling_max_freq"
echo 305000000 > "${gpu}/max_freq"
run cpu-restore
[[ "${rc}" == 0 ]] || fail "cpu-restore without a file exit ${rc}"
expect_online 1 1 1 1 1 1 1
expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 518400
expect_value "${gpu}/max_freq" 305000000
[[ ! -e "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "cpu-restore without a file created one"
[[ ! -e "${T}/run" ]] || fail "cpu-restore without a file created the directory"

case_name="cpu-dap with a refused write"
reset
chmod 0444 "${cpu}/cpu5/online"
run cpu-dap
[[ "${rc}" == 1 ]] || fail "cpu-dap with a refused write exit ${rc}, expected 1"
expect_saved
expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 787200
expect_online 1 1 1 0 1 0 0
expect_value "${gpu}/governor" simple_ondemand
expect_value "${gpu}/max_freq" 305000000
chmod 0644 "${cpu}/cpu5/online"

case_name="cpu-restore with a refused write"
chmod 0444 "${cpu}/cpufreq/policy0/scaling_max_freq"
run cpu-restore
[[ "${rc}" == 1 ]] || fail "cpu-restore with a refused write exit ${rc}, expected 1"
expect_online 1 1 1 1 1 1 1
expect_value "${cpu}/cpufreq/policy0/scaling_max_freq" 787200
expect_value "${gpu}/governor" userspace
expect_value "${gpu}/max_freq" 587000000
[[ -f "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "cpu-restore with a refused write removed the save file"
chmod 0644 "${cpu}/cpufreq/policy0/scaling_max_freq"

case_name="cpu-restore retry"
run cpu-restore
[[ "${rc}" == 0 ]] || fail "cpu-restore retry exit ${rc}"
expect_untouched_sysfs
[[ ! -e "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "cpu-restore retry left the save file"

for verb in bogus "" "cores-offline;id" "bt-class-headphones;id" "backlight-off --help" "cpu-dap;id" cpu "cpu-restore " CPU-DAP; do
    case_name="verb '${verb}'"
    reset
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
    [[ -s "${STATE}" ]] && fail "verb '${verb}' ran $(cat "${STATE}")" || true
    [[ "$(cat "${sys}/class/backlight/panel0/bl_power")" == 0 ]] || fail "verb '${verb}' touched sysfs"
    expect_untouched_sysfs
    [[ ! -e "${T}/run" ]] || fail "verb '${verb}' wrote a save file"
done

case_name="no verb"
reset
run
[[ "${rc}" == 1 ]] || fail "no verb exit ${rc}, expected 1"

case_name="two verbs"
run cpu-dap cpu-off
[[ "${rc}" == 1 ]] || fail "two verbs exit ${rc}, expected 1"
expect_untouched_sysfs

if (( failures )); then
    echo "priv-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "priv-test: ok"
