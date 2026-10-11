#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
helper="${repo}/sysext/usr/libexec/casdap/casdap-usb-audio"
rules="${repo}/sysext/usr/lib/udev/rules.d/70-casdap-usb-audio.rules"
unit="${repo}/sysext/usr/lib/systemd/system/casdap-usb-audio.service"
wants="${repo}/sysext/usr/lib/systemd/system/multi-user.target.d/50-casdap.conf"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
mkdir -p "${T}/bin"
export LOG="${T}/log"
export CASDAP_SYS_ROOT="${T}/sys"
R="${CASDAP_SYS_ROOT}"

cat > "${T}/bin/logger" <<'STUB'
#!/usr/bin/bash
[[ "$1" == -t && "$3" == -- ]] || { echo "bad logger args: $*" >> "${LOG}"; exit 0; }
echo "$2: ${*:4}" >> "${LOG}"
STUB
chmod 0755 "${T}/bin/logger"
export PATH="${T}/bin:${PATH}"

fiio='05 0c 09 01 a1 01 85 03 15 00 25 03 75 01 95 02 09 e9 09 ea 81 02 95 04 09 cd 09 cf 09 b5 09 b6 81 02 95 02 81 01 06 01 ff 85 4b 75 08 95 3f 09 01 81 03 95 3f 09 02 91 02 85 07 75 08 95 3f 09 03 81 03 95 3f 09 04 91 02 c0'
gamepad='05 01 09 05 a1 01 85 01 09 30 09 31 15 00 26 ff 00 75 08 95 02 81 02 c0'
mouse='05 01 09 02 a1 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 c0 c0'

failures=0
case_name=
fail() { echo "FAIL ${case_name}: $*" >&2; echo "--- log" >&2; cat "${LOG}" >&2; failures=$((failures + 1)); }

begin() {
    case_name=$1
    rm -rf "${R}" "${LOG}"
    : > "${LOG}"
    mkdir -p "${R}/bus/usb/drivers/usbhid" "${R}/bus/usb/drivers/snd-usb-audio" "${R}/bus/usb/devices" "${R}/devices/usb3"
    : > "${R}/bus/usb/drivers/usbhid/unbind"
}

device() {
    local name=$1 vid=$2 pid=$3 dev="${R}/devices/usb3/$1"
    mkdir -p "${dev}/power"
    echo "${vid}" > "${dev}/idVendor"
    echo "${pid}" > "${dev}/idProduct"
    echo on > "${dev}/power/control"
    echo 100 > "${dev}/power/autosuspend_delay_ms"
    ln -s "../../../devices/usb3/${name}" "${R}/bus/usb/devices/${name}"
}

intf() {
    local dev="${R}/devices/usb3/$1" n=$2 class=$3 driver=$4 desc=${5:-} i hex bytes=
    i="${dev}/$1:1.${n}"
    mkdir -p "${i}"
    echo "${class}" > "${i}/bInterfaceClass"
    [[ -n "${driver}" ]] && ln -s "${R}/bus/usb/drivers/${driver}" "${i}/driver"
    if [[ "${class}" == 03 ]]; then
        mkdir -p "${i}/0003:0000:0000.0001"
        if [[ "${desc}" != none ]]; then
            for hex in ${desc}; do
                bytes+="\\x${hex}"
            done
            printf '%b' "${bytes}" > "${i}/0003:0000:0000.0001/report_descriptor"
        fi
    fi
    return 0
}

dac() {
    device "$1" 2972 0149
    intf "$1" 0 03 usbhid "${2:-${fiio}}"
    intf "$1" 1 01 snd-usb-audio
    intf "$1" 2 01 snd-usb-audio
}

run() {
    rc=0
    "${helper}" "$@" || rc=$?
}

devpath() { echo "${R}/devices/usb3/$1"; }
expect_rc() { [[ "${rc}" == "$1" ]] || fail "exit ${rc}, expected $1"; }
expect_unbind() { [[ "$(cat "${R}/bus/usb/drivers/usbhid/unbind")" == "$1" ]] || fail "unbind holds '$(cat "${R}/bus/usb/drivers/usbhid/unbind")', expected '$1'"; }
expect_power() {
    local dev
    dev=$(devpath "$1")
    [[ "$(cat "${dev}/power/control")" == "$2" ]] || fail "$1 power/control is $(cat "${dev}/power/control"), expected $2"
    [[ "$(cat "${dev}/power/autosuspend_delay_ms")" == "$3" ]] || fail "$1 autosuspend_delay_ms is $(cat "${dev}/power/autosuspend_delay_ms"), expected $3"
}
expect_log() {
    local expected
    expected=$(printf '%s\n' "$@")
    [[ "$(cat "${LOG}")" == "${expected}" ]] || fail "log was:"$'\n'"$(cat "${LOG}")"$'\n'"expected:"$'\n'"${expected}"
}
expect_handled() {
    expect_unbind "$1:1.0"
    expect_power "$1" auto 2000
    expect_log "casdap-usb-audio: $1 2972:0149 hid $1:1.0 unbound" "casdap-usb-audio: $1 autosuspend on"
}
expect_untouched() {
    expect_unbind ""
    expect_power "$1" on 100
}
expect_skipped() {
    expect_untouched "$1"
    expect_log "casdap-usb-audio: $1 skipped, generic desktop hid"
}

begin fiio
dac 3-1
run "$(devpath 3-1)"
expect_rc 0
expect_handled 3-1

begin dualsense
device 3-1 054c 0ce6
intf 3-1 0 01 snd-usb-audio
intf 3-1 1 01 snd-usb-audio
intf 3-1 2 01 snd-usb-audio
intf 3-1 3 03 usbhid "${gamepad}"
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin mouse
device 3-1 046d c077
intf 3-1 0 03 usbhid "${mouse}"
run "$(devpath 3-1)"
expect_rc 0
expect_untouched 3-1
expect_log

begin audio-only
device 3-1 05ac 110a
intf 3-1 0 01 snd-usb-audio
intf 3-1 1 01 snd-usb-audio
run "$(devpath 3-1)"
expect_rc 0
expect_untouched 3-1
expect_log

begin second-run
dac 3-1
rm "$(devpath 3-1)/3-1:1.0/driver"
echo auto > "$(devpath 3-1)/power/control"
echo 2000 > "$(devpath 3-1)/power/autosuspend_delay_ms"
chmod 0444 "$(devpath 3-1)/power/control" "$(devpath 3-1)/power/autosuspend_delay_ms"
touch -d 2000-01-01 "$(devpath 3-1)/power/control" "$(devpath 3-1)/power/autosuspend_delay_ms"
run "$(devpath 3-1)"
expect_rc 0
expect_unbind ""
[[ "$(stat -c %Y "$(devpath 3-1)/power/control")" == "$(date -d 2000-01-01 +%s)" ]] || fail "power/control rewritten"
[[ "$(stat -c %Y "$(devpath 3-1)/power/autosuspend_delay_ms")" == "$(date -d 2000-01-01 +%s)" ]] || fail "autosuspend_delay_ms rewritten"
expect_log

begin scan
dac 3-1
device 3-2 054c 0ce6
intf 3-2 0 01 snd-usb-audio
intf 3-2 3 03 usbhid "${gamepad}"
device usb3 1d6b 0002
intf usb3 0 09 hub
run --scan
expect_rc 0
expect_unbind "3-1:1.0"
expect_power 3-1 auto 2000
expect_power 3-2 on 100
expect_power usb3 on 100
expect_log "casdap-usb-audio: 3-1 2972:0149 hid 3-1:1.0 unbound" "casdap-usb-audio: 3-1 autosuspend on" "casdap-usb-audio: 3-2 skipped, generic desktop hid"

begin unreadable
dac 3-1 none
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin truncated
dac 3-1 '05 0c 09 01 a1 01 26 ff'
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin empty
dac 3-1 ''
: > "$(devpath 3-1)/3-1:1.0/0003:0000:0000.0001/report_descriptor"
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin unbind-fails
dac 3-1
rm "${R}/bus/usb/drivers/usbhid/unbind"
mkdir "${R}/bus/usb/drivers/usbhid/unbind"
run "$(devpath 3-1)"
expect_rc 0
expect_power 3-1 auto 2000
expect_log "casdap-usb-audio: 3-1 2972:0149 hid 3-1:1.0 unbind failed" "casdap-usb-audio: 3-1 autosuspend on"

begin gd-page-2-bytes
dac 3-1 '06 01 00 09 05 a1 01 c0'
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin gd-usage-4-bytes
dac 3-1 '0b 05 00 01 00 a1 01 c0'
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin gd-in-item-data
dac 3-1 '05 0c 09 01 a1 01 15 00 26 05 01 75 10 95 01 09 e9 81 02 c0'
run "$(devpath 3-1)"
expect_rc 0
expect_handled 3-1

begin long-item
dac 3-1 'fe 03 00 05 01 01 05 0c 09 01 a1 01 c0'
run "$(devpath 3-1)"
expect_rc 0
expect_handled 3-1

begin gd-after-long-item
dac 3-1 'fe 02 00 aa bb 05 01 09 05 a1 01 c0'
run "$(devpath 3-1)"
expect_rc 0
expect_skipped 3-1

begin add-no-interfaces
device 3-1 2972 0149
run "$(devpath 3-1)"
expect_rc 0
expect_untouched 3-1
expect_log

begin interface-bind-path
dac 3-1
run "$(devpath 3-1)/3-1:1.0/.."
expect_rc 0
expect_handled 3-1

begin no-argument
run 2>/dev/null
expect_rc 2
expect_log

case_name=files
for f in "${helper}" "${rules}" "${unit}" "${wants}"; do
    if LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}"; then
        fail "${f} has a dash"
    fi
    if grep -v '^#!' "${f}" | grep -qE '^\s*#'; then
        fail "${f} has a comment"
    fi
done
grep -qxF 'ACTION=="add|bind", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", RUN+="/usr/libexec/casdap/casdap-usb-audio %S%p"' "${rules}" || fail "rules lack the usb_device line"
grep -qxF 'ACTION=="bind", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_interface", DRIVER=="usbhid", RUN+="/usr/libexec/casdap/casdap-usb-audio %S%p/.."' "${rules}" || fail "rules lack the usbhid interface line"
grep -qxF 'ACTION!="remove", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="2972", ATTR{idProduct}=="0149", GROUP="audio", MODE="0660", TAG+="systemd"' "${rules}" || fail "rules lack the fiio node line"
grep -qxF 'ACTION=="add", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="2972", ATTR{idProduct}=="0149", ENV{SYSTEMD_USER_WANTS}+="casdap-fiio-reset.service"' "${rules}" || fail "rules lack the fiio plug in line"
grep -qxF 'ExecStart=/usr/libexec/casdap/casdap-usb-audio --scan' "${unit}" || fail "unit lacks the scan"
grep -qxF 'RemainAfterExit=yes' "${unit}" || fail "unit lacks RemainAfterExit=yes"
grep -qxF 'Upholds=casdap-guard.service casdap-usb-audio.service' "${wants}" || fail "multi-user drop in lacks the Upholds line"
grep -qxF 'EXTENSION_RELOAD_MANAGER=1' "${repo}/sysext/usr/lib/extension-release.d/extension-release.casdap" \
    || fail "extension-release does not ask for a manager reload"

if (( failures )); then
    echo "usb-audio-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "usb-audio-test: ok"
