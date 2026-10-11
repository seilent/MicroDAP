#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
unit="${repo}/sysext/usr/lib/systemd/user/casdap-fiio-reset.service"
stop="${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@casdap.service.d/05-casdap-fiio.conf"
start="${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@steam.service.d/40-casdap-fiio.conf"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

has() {
    [[ "$(grep -c -E -- "$1" "$2" || true)" == 1 ]] || fail "$(basename "$2"): expected one line matching '$1'"
}

for f in "${unit}" "${stop}" "${start}"; do
    [[ -f "${f}" ]] || { echo "FAIL missing ${f}" >&2; exit 1; }
done

has '^\[Unit\]$' "${unit}"
has '^Description=CASDAP FiiO bypass on plug in$' "${unit}"
has '^ConditionUser=!root$' "${unit}"
has '^ConditionGroup=audio$' "${unit}"
has '^\[Service\]$' "${unit}"
has '^Type=oneshot$' "${unit}"
has '^ExecStart=/usr/libexec/casdap/casdap-fiio reset$' "${unit}"
[[ "$(grep -c -v -E '^(\[Unit\]|Description=CASDAP FiiO bypass on plug in|ConditionUser=!root|ConditionGroup=audio|\[Service\]|Type=oneshot|ExecStart=/usr/libexec/casdap/casdap-fiio reset|)$' "${unit}" || true)" == 0 ]] || fail "unit has extra lines"

[[ "$(cat "${stop}")" == $'[Service]\nExecStopPost=-/usr/libexec/casdap/casdap-fiio reset --no-wait --deadline 3500 --stamp' ]] || fail "$(basename "${stop}"): expected the ExecStopPost reset only"
[[ "$(cat "${start}")" == $'[Service]\nExecStartPre=-/usr/libexec/casdap/casdap-fiio reset --no-wait --deadline 1000 --skip-stamped' ]] || fail "$(basename "${start}"): expected the ExecStartPre reset only"
first=$(find "${repo}/sysext/usr/lib/systemd/user/gamescope-session-plus@casdap.service.d" -name '*.conf' -printf '%f\n' | LC_ALL=C sort | head -n 1)
[[ "${first}" == 05-casdap-fiio.conf ]] || fail "05-casdap-fiio.conf does not sort first, ${first} does"

for f in "${unit}" "${stop}" "${start}"; do
    grep -q '#' "${f}" && fail "$(basename "${f}"): comment" || true
    LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}" && fail "$(basename "${f}"): dash" || true
done

if (( failures )); then
    echo "fiio-unit-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "fiio-unit-test: ok"
