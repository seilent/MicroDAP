#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
unit="${repo}/data/systemd/user/microdap-overlay.service"
preset="${repo}/data/systemd/user-preset/70-microdap.preset"
keep="${repo}/data/libexec/microdap-keep"
waitx="${repo}/data/libexec/microdap-wait-x"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

has() {
    [[ "$(grep -c -E -- "$1" "$2" || true)" == 1 ]] || fail "$(basename "$2"): expected one line matching '$1'"
}

lacks() {
    if grep -q -E -- "$1" "$2"; then fail "$(basename "$2"): unexpected match for '$1'"; fi
}

[[ -f "${unit}" ]] || { echo "FAIL missing ${unit}" >&2; exit 1; }
[[ -f "${preset}" ]] || { echo "FAIL missing ${preset}" >&2; exit 1; }
[[ -x "${keep}" ]] || fail "microdap-keep is not executable"
[[ -x "${waitx}" ]] || fail "microdap-wait-x is not executable"

has '^BindsTo=gamescope-session-plus@steam\.service$' "${unit}"
has '^After=gamescope-session-plus@steam\.service$' "${unit}"
has '^Conflicts=gamescope-session-plus@microdap\.service$' "${unit}"
has '^After=gamescope-session-plus@microdap\.service$' "${unit}"
has '^StartLimitIntervalSec=0$' "${unit}"
has '^Type=notify$' "${unit}"
has '^NotifyAccess=main$' "${unit}"
has '^WatchdogSec=15$' "${unit}"
has '^TimeoutStartSec=40$' "${unit}"
has '^Environment=DISPLAY=:0$' "${unit}"
has '^Environment=QT_QPA_PLATFORM=xcb$' "${unit}"
has '^ExecCondition=/usr/libexec/microdap/microdap-keep check$' "${unit}"
has '^ExecStartPre=.*microdap-wait-x$' "${unit}"
has '^ExecStart=/usr/bin/microdap-player --overlay$' "${unit}"
has '^Restart=on-failure$' "${unit}"
has '^RestartSec=2$' "${unit}"
lacks 'MICRODAP_GAMESCOPE_APPID' "${unit}"
lacks 'STEAM_GAME' "${unit}"
lacks '^ExecStop' "${unit}"
lacks 'intercept' "${unit}"
has '^\[Install\]$' "${unit}"
has '^WantedBy=gamescope-session-plus@steam\.service$' "${unit}"
lacks '^#' "${unit}"

has '^enable microdap-overlay\.service$' "${preset}"
[[ ! -e "${repo}/data/systemd/user/gamescope-session-plus@steam.service.d/10-microdap-overlay.conf" ]] || fail "overlay Wants drop in still present"

for p in "${unit}" "${preset}" "${keep}" "${waitx}"; do
    if LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${p}"; then fail "$(basename "${p}"): dash"; fi
done

if (( failures )); then
    echo "overlay-unit-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "overlay-unit-test: ok"
