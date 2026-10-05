#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
units="${repo}/sysext/usr/lib/systemd/user"
unit="${units}/casdap-overlay.service"
dropin="${units}/gamescope-session-plus@steam.service.d/10-casdap-overlay.conf"
keep="${repo}/sysext/usr/libexec/casdap/casdap-keep"
waitx="${repo}/sysext/usr/libexec/casdap/casdap-wait-x"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

has() {
    [[ "$(grep -c -E -- "$1" "$2" || true)" == 1 ]] || fail "$(basename "$2"): expected one line matching '$1'"
}

lacks() {
    grep -q -E -- "$1" "$2" && fail "$(basename "$2"): unexpected match for '$1'" || true
}

[[ -f "${unit}" ]] || { echo "FAIL missing ${unit}" >&2; exit 1; }
[[ -f "${dropin}" ]] || { echo "FAIL missing ${dropin}" >&2; exit 1; }
[[ -x "${keep}" ]] || fail "casdap-keep is not executable"
[[ -x "${waitx}" ]] || fail "casdap-wait-x is not executable"

has '^BindsTo=gamescope-session-plus@steam\.service$' "${unit}"
has '^After=gamescope-session-plus@steam\.service$' "${unit}"
has '^Conflicts=gamescope-session-plus@casdap\.service$' "${unit}"
has '^After=gamescope-session-plus@casdap\.service$' "${unit}"
has '^StartLimitIntervalSec=0$' "${unit}"
has '^Type=notify$' "${unit}"
has '^NotifyAccess=main$' "${unit}"
has '^WatchdogSec=15$' "${unit}"
has '^TimeoutStartSec=40$' "${unit}"
has '^Environment=DISPLAY=:0$' "${unit}"
has '^Environment=QT_QPA_PLATFORM=xcb$' "${unit}"
has '^ExecCondition=/usr/libexec/casdap/casdap-keep check$' "${unit}"
has '^ExecStartPre=.*casdap-wait-x$' "${unit}"
has '^ExecStart=/usr/bin/casdap-player --overlay$' "${unit}"
has '^ExecStopPost=/usr/libexec/casdap/casdap-intercept 0$' "${unit}"
has '^Restart=on-failure$' "${unit}"
has '^RestartSec=2$' "${unit}"
lacks 'CASDAP_GAMESCOPE_APPID' "${unit}"
lacks 'STEAM_GAME' "${unit}"
lacks '^ExecStop=' "${unit}"
lacks '^#' "${unit}"

has '^\[Unit\]$' "${dropin}"
has '^Wants=casdap-overlay\.service$' "${dropin}"
[[ "$(grep -c -v -E '^(\[Unit\]|Wants=casdap-overlay\.service)$' "${dropin}" || true)" == 0 ]] || fail "drop in has extra lines"

for p in "${unit}" "${dropin}" "${keep}" "${waitx}"; do
    LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${p}" && fail "$(basename "${p}"): dash" || true
done

template=/usr/lib/systemd/user/gamescope-session-plus@.service
if command -v systemd-analyze > /dev/null && [[ -f "${template}" && -x /usr/bin/casdap-player ]]; then
    systemd-analyze --user verify "${unit}" || fail "systemd-analyze verify"
else
    echo "skip: systemd-analyze --user verify (needs ${template} and /usr/bin/casdap-player)"
fi

if (( failures )); then
    echo "overlay-unit-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "overlay-unit-test: ok"
