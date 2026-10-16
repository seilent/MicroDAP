#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
unit="${repo}/data/systemd/user/microdap-fiio-reset.service"
stop="${repo}/data/systemd/user/gamescope-session-plus@microdap.service.d/05-microdap-fiio.conf"
start="${repo}/data/systemd/user/gamescope-session-plus@steam.service.d/40-microdap-fiio.conf"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

has() {
    [[ "$(grep -c -E -- "$1" "$2" || true)" == 1 ]] || fail "$(basename "$2"): expected one line matching '$1'"
}

for f in "${unit}" "${stop}" "${start}"; do
    [[ -f "${f}" ]] || { echo "FAIL missing ${f}" >&2; exit 1; }
done

has '^\[Unit\]$' "${unit}"
has '^Description=MicroDAP FiiO bypass on plug in$' "${unit}"
has '^ConditionUser=!root$' "${unit}"
has '^ConditionGroup=audio$' "${unit}"
has '^\[Service\]$' "${unit}"
has '^Type=oneshot$' "${unit}"
has '^ExecStart=/usr/libexec/microdap/microdap-fiio reset$' "${unit}"
[[ "$(grep -c -v -E '^(\[Unit\]|Description=MicroDAP FiiO bypass on plug in|ConditionUser=!root|ConditionGroup=audio|\[Service\]|Type=oneshot|ExecStart=/usr/libexec/microdap/microdap-fiio reset|)$' "${unit}" || true)" == 0 ]] || fail "unit has extra lines"

[[ "$(cat "${stop}")" == $'[Service]\nExecStopPost=-/usr/libexec/microdap/microdap-fiio reset --no-wait --deadline 3500 --stamp' ]] || fail "$(basename "${stop}"): expected the ExecStopPost reset only"
[[ "$(cat "${start}")" == $'[Service]\nExecStartPre=-/usr/libexec/microdap/microdap-fiio reset --no-wait --deadline 1000 --skip-stamped' ]] || fail "$(basename "${start}"): expected the ExecStartPre reset only"
first=$(find "${repo}/data/systemd/user/gamescope-session-plus@microdap.service.d" -name '*.conf' -printf '%f\n' | LC_ALL=C sort | head -n 1)
[[ "${first}" == 05-microdap-fiio.conf ]] || fail "05-microdap-fiio.conf does not sort first, ${first} does"

for f in "${unit}" "${stop}" "${start}"; do
    if grep -q '#' "${f}"; then fail "$(basename "${f}"): comment"; fi
    if LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}"; then fail "$(basename "${f}"): dash"; fi
done

if (( failures )); then
    echo "fiio-unit-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "fiio-unit-test: ok"
