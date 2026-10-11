#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
unit="${repo}/sysext/usr/lib/systemd/user/casdap-fiio-reset.service"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

has() {
    [[ "$(grep -c -E -- "$1" "$2" || true)" == 1 ]] || fail "$(basename "$2"): expected one line matching '$1'"
}

[[ -f "${unit}" ]] || { echo "FAIL missing ${unit}" >&2; exit 1; }

has '^\[Unit\]$' "${unit}"
has '^Description=CASDAP FiiO bypass on plug in$' "${unit}"
has '^\[Service\]$' "${unit}"
has '^Type=oneshot$' "${unit}"
has '^ExecStart=/usr/libexec/casdap/casdap-fiio reset$' "${unit}"
[[ "$(grep -c -v -E '^(\[Unit\]|Description=CASDAP FiiO bypass on plug in|\[Service\]|Type=oneshot|ExecStart=/usr/libexec/casdap/casdap-fiio reset|)$' "${unit}" || true)" == 0 ]] || fail "unit has extra lines"
grep -q '#' "${unit}" && fail "$(basename "${unit}"): comment" || true
LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${unit}" && fail "$(basename "${unit}"): dash" || true

if (( failures )); then
    echo "fiio-unit-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "fiio-unit-test: ok"
