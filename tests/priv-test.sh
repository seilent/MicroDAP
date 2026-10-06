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
export CASDAP_AMIXER_BIN="${T}/bin/amixer"
export CASDAP_RGB_BIN="${T}/bin/armada-rgb"
export CASDAP_RGB_CONFIG="${T}/etc/armada/rgb.json"
export CASDAP_RGB_BEFORE_FILE="${T}/run/rgb-before.json"
export RGB_GET="${T}/rgb-get"
export AMIXER_FAIL="${T}/amixer-fail"
export RGB_FAIL="${T}/rgb-fail"

cat > "${T}/bin/btmgmt" <<'EOF'
#!/usr/bin/bash
echo "btmgmt $*" >> "${STATE}"
EOF
cat > "${T}/bin/amixer" <<'EOF'
#!/usr/bin/bash
{ printf 'amixer'; printf '[%s]' "$@"; echo; } >> "${STATE}"
[[ -e "${AMIXER_FAIL}" ]] && { echo "amixer: Cannot find the given element from control hw:0" >&2; exit 1; }
echo "numid=1,iface=MIXER,name='$4'"
EOF
cat > "${T}/bin/armada-rgb" <<'EOF'
#!/usr/bin/bash
{ printf 'armada-rgb'; printf '[%s]' "$@"; echo; } >> "${STATE}"
case "$1" in
    get)
        [[ -e "${RGB_FAIL}" && "$(cat "${RGB_FAIL}")" == get ]] && { echo "Error: no profile" >&2; exit 1; }
        cat "${RGB_GET}"
        ;;
    set|off|apply|sleep|wake)
        [[ -e "${RGB_FAIL}" && "$(cat "${RGB_FAIL}")" == "$1" ]] && { echo "Error: /dev/ttyHS2 busy" >&2; exit 1; }
        ;;
esac
exit 0
EOF
chmod 0755 "${T}/bin/btmgmt" "${T}/bin/amixer" "${T}/bin/armada-rgb"
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
    rm -rf "${T}/run" "${T}/etc" "${AMIXER_FAIL}" "${RGB_FAIL}"
    printf '{\n  "version": 1,\n  "enabled": true,\n  "linkBrightness": true,\n  "brightness": 30,\n  "maxBrightness": 60,\n  "color": "FFFFFF",\n  "saturation": 100\n}\n' > "${RGB_GET}"
}

rgb_config() {
    mkdir -p "$(dirname "${CASDAP_RGB_CONFIG}")"
    printf '%s\n' "$1" > "${CASDAP_RGB_CONFIG}"
}

expect_calls() {
    local expected
    expected=$(printf '%s\n' "$@")
    [[ "$(cat "${STATE}")" == "${expected}" ]] || fail "${case_name}: calls were:"$'\n'"$(cat "${STATE}")"$'\n'"expected:"$'\n'"${expected}"
}

expect_no_rgb_files() {
    [[ ! -e "${CASDAP_RGB_BEFORE_FILE}" ]] || fail "${case_name}: save file written"
    [[ ! -e "${CASDAP_RGB_BEFORE_FILE}.tmp" ]] || fail "${case_name}: save temp file left"
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

for pair in "low 8" "normal 20" "high 24"; do
    read -r name value <<< "${pair}"
    case_name="hp-gain-${name}"
    reset
    run "hp-gain-${name}"
    [[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
    expect_calls "amixer[-D][hw:0][cset][name=HPHL Volume][${value}]" "amixer[-D][hw:0][cset][name=HPHR Volume][${value}]"
    expect_untouched_sysfs
    [[ ! -e "${T}/run" ]] || fail "${case_name} wrote a save file"
done

for triple in "lohifi CLS_H_LOHIFI LOHIFI" "ulp CLS_H_ULP ULP" "hifi CLS_H_HIFI LOHIFI"; do
    read -r name mode pwr <<< "${triple}"
    case_name="hp-amp-${name}"
    reset
    run "hp-amp-${name}"
    [[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
    expect_calls "amixer[-D][hw:0][cset][name=RX HPH Mode][${mode}]" "amixer[-D][hw:0][cset][name=RX_HPH PWR Mode][${pwr}]"
    expect_untouched_sysfs
done

for step in 84 90 96 99 102 105 108; do
    case_name="spk-gain-${step}"
    reset
    run "spk-gain-${step}"
    [[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
    expect_calls "amixer[-D][hw:0][cset][name=WSA_RX0 Digital Volume][${step}]" "amixer[-D][hw:0][cset][name=WSA_RX1 Digital Volume][${step}]"
    expect_untouched_sysfs
done

case_name="hp-gain-low with a refused cset"
reset
: > "${AMIXER_FAIL}"
run hp-gain-low
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
expect_calls "amixer[-D][hw:0][cset][name=HPHL Volume][8]" "amixer[-D][hw:0][cset][name=HPHR Volume][8]"

case_name="spk-gain-102 with a refused cset"
reset
: > "${AMIXER_FAIL}"
run spk-gain-102
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
expect_calls "amixer[-D][hw:0][cset][name=WSA_RX0 Digital Volume][102]" "amixer[-D][hw:0][cset][name=WSA_RX1 Digital Volume][102]"

accent_set="armada-rgb[set][--enabled][true][--link-brightness][true][--color]"
rgb_get="armada-rgb[get]"
rgb_off="armada-rgb[off]"
rgb_apply="armada-rgb[apply]"
steam_config='{"version":1,"enabled":true,"linkBrightness":false,"brightness":40,"maxBrightness":50,"color":"00FF00","saturation":100}'

accent_call() { printf '%s[%s][--brightness][%s][--max-brightness][%s]' "${accent_set}" "$1" "$2" "$3"; }

for pair in "pink FA2A55" "orange F09609" "cyan 1BA1E2" "lime 339933" "purple A200FF"; do
    read -r name hex <<< "${pair}"
    case_name="rgb-accent-${name}"
    reset
    rgb_config "${steam_config}"
    run "rgb-accent-${name}"
    [[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
    expect_calls "${rgb_get}" "$(accent_call "${hex}" 30 60)"
    [[ -f "${CASDAP_RGB_BEFORE_FILE}" ]] || fail "${case_name}: no save file"
    [[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save differs from the config"
    [[ ! -e "${CASDAP_RGB_BEFORE_FILE}.tmp" ]] || fail "${case_name}: save temp file left"
    [[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config rewritten by the word"
    expect_untouched_sysfs
    [[ ! -e "${CASDAP_CPU_BEFORE_FILE}" ]] || fail "${case_name} wrote the cpu save file"
done

case_name="rgb-accent-cyan with a save present"
reset
rgb_config "${steam_config}"
mkdir -p "${T}/run"
echo earlier > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-accent-cyan
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == earlier ]] || fail "${case_name}: save rewritten"
expect_calls "${rgb_get}" "$(accent_call 1BA1E2 30 60)"

case_name="rgb-accent-lime without a config"
reset
run rgb-accent-lime
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ -f "${CASDAP_RGB_BEFORE_FILE}" ]] || fail "${case_name}: no save file"
[[ ! -s "${CASDAP_RGB_BEFORE_FILE}" ]] || fail "${case_name}: save not zero byte"
[[ ! -e "${CASDAP_RGB_CONFIG}" ]] || fail "${case_name}: config created by the word"
expect_calls "${rgb_get}" "$(accent_call 339933 30 60)"

case_name="rgb-accent-pink with maxBrightness 0"
reset
rgb_config "${steam_config}"
printf '{\n  "version": 1,\n  "enabled": false,\n  "linkBrightness": false,\n  "brightness": 0,\n  "maxBrightness": 0,\n  "color": "FFFFFF",\n  "saturation": 100\n}\n' > "${RGB_GET}"
run rgb-accent-pink
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_get}" "$(accent_call FA2A55 0 50)"

case_name="rgb-accent-cyan with a failing set"
reset
rgb_config "${steam_config}"
echo set > "${RGB_FAIL}"
run rgb-accent-cyan
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save missing or changed"
expect_calls "${rgb_get}" "$(accent_call 1BA1E2 30 60)"

case_name="rgb-accent-cyan with a failing get"
reset
rgb_config "${steam_config}"
echo get > "${RGB_FAIL}"
run rgb-accent-cyan
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save missing or changed"
expect_calls "${rgb_get}"

case_name="rgb-accent-cyan with a get lacking brightness"
reset
rgb_config "${steam_config}"
printf '{\n  "version": 1\n}\n' > "${RGB_GET}"
run rgb-accent-cyan
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
expect_calls "${rgb_get}"

case_name="rgb-off"
reset
rgb_config "${steam_config}"
run rgb-off
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_off}"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save differs from the config"
expect_untouched_sysfs

case_name="rgb-off without a config"
reset
run rgb-off
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ -f "${CASDAP_RGB_BEFORE_FILE}" && ! -s "${CASDAP_RGB_BEFORE_FILE}" ]] || fail "${case_name}: save not zero byte"
expect_calls "${rgb_off}"

case_name="rgb-off with a save present"
reset
rgb_config "${steam_config}"
mkdir -p "${T}/run"
echo earlier > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-off
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == earlier ]] || fail "${case_name}: save rewritten"

case_name="rgb-off with a failing off"
reset
rgb_config "${steam_config}"
echo off > "${RGB_FAIL}"
run rgb-off
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save missing or changed"

case_name="rgb-sleep"
reset
rgb_config "${steam_config}"
run rgb-sleep
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "armada-rgb[sleep]"
expect_no_rgb_files
[[ ! -e "${T}/run" ]] || fail "${case_name} created the save directory"
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config touched"
expect_untouched_sysfs

case_name="rgb-wake"
reset
rgb_config "${steam_config}"
run rgb-wake
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "armada-rgb[wake]"
expect_no_rgb_files
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config touched"

case_name="rgb-sleep with a save present"
reset
mkdir -p "${T}/run"
echo earlier > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-sleep
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == earlier ]] || fail "${case_name}: save touched"

case_name="rgb-wake with a failing wake"
reset
echo wake > "${RGB_FAIL}"
run rgb-wake
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
expect_no_rgb_files

case_name="rgb-restore with a saved config"
reset
rgb_config '{"version":1,"enabled":true,"brightness":30,"color":"1BA1E2"}'
mkdir -p "${T}/run"
printf '%s\n' "${steam_config}" > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_apply}"
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config not restored"
[[ ! -e "${CASDAP_RGB_CONFIG}.tmp" ]] || fail "${case_name}: config temp file left"
expect_no_rgb_files
expect_untouched_sysfs

case_name="rgb-restore with a zero byte save"
reset
rgb_config '{"version":1,"enabled":true,"brightness":30,"color":"1BA1E2"}'
mkdir -p "${T}/run"
: > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_apply}"
[[ ! -e "${CASDAP_RGB_CONFIG}" ]] || fail "${case_name}: config left"
expect_no_rgb_files

case_name="rgb-restore with a zero byte save and no config"
reset
mkdir -p "${T}/run"
: > "${CASDAP_RGB_BEFORE_FILE}"
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_apply}"
expect_no_rgb_files

case_name="rgb-restore without a save"
reset
rgb_config "${steam_config}"
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
[[ ! -s "${STATE}" ]] || fail "${case_name} ran $(cat "${STATE}")"
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config touched"
[[ ! -e "${T}/run" ]] || fail "${case_name} created the save directory"

case_name="rgb-restore with a failing apply"
reset
rgb_config '{"version":1,"enabled":true,"brightness":30,"color":"1BA1E2"}'
mkdir -p "${T}/run"
printf '%s\n' "${steam_config}" > "${CASDAP_RGB_BEFORE_FILE}"
echo apply > "${RGB_FAIL}"
run rgb-restore
[[ "${rc}" == 1 ]] || fail "${case_name} exit ${rc}, expected 1"
expect_calls "${rgb_apply}"
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config not restored"
[[ "$(cat "${CASDAP_RGB_BEFORE_FILE}")" == "${steam_config}" ]] || fail "${case_name}: save removed"

case_name="rgb-restore retry"
rm -f "${RGB_FAIL}"
: > "${STATE}"
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_apply}"
expect_no_rgb_files

case_name="rgb-accent then rgb-restore"
reset
rgb_config "${steam_config}"
run rgb-accent-purple
run rgb-off
run rgb-restore
[[ "${rc}" == 0 ]] || fail "${case_name} exit ${rc}"
expect_calls "${rgb_get}" "$(accent_call A200FF 30 60)" "${rgb_off}" "${rgb_apply}"
[[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "${case_name}: config not restored"
expect_no_rgb_files

for verb in bogus "" "cores-offline;id" "bt-class-headphones;id" "backlight-off --help" "cpu-dap;id" cpu "cpu-restore " CPU-DAP \
    hp-gain hp-gain-8 hp-gain-medium hp-amp hp-amp-cls_h_ulp spk-gain spk-gain-120 spk-gain-0 "spk-gain-102 " \
    rgb rgb-accent rgb-accent-red rgb-accent-FA2A55 "rgb-restore;id" RGB-OFF "rgb-sleep "; do
    case_name="verb '${verb}'"
    reset
    rgb_config "${steam_config}"
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
    [[ -s "${STATE}" ]] && fail "verb '${verb}' ran $(cat "${STATE}")" || true
    [[ "$(cat "${sys}/class/backlight/panel0/bl_power")" == 0 ]] || fail "verb '${verb}' touched sysfs"
    expect_untouched_sysfs
    [[ ! -e "${T}/run" ]] || fail "verb '${verb}' wrote a save file"
    [[ "$(cat "${CASDAP_RGB_CONFIG}")" == "${steam_config}" ]] || fail "verb '${verb}' touched the rgb config"
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
