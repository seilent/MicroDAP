#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
keep="${repo}/data/libexec/microdap-keep"

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
export MICRODAP_KEEP_FILE="${T}/keep"
export MICRODAP_BOOT_ID_FILE="${T}/boot_id"
boot_id=5c1a4f9e-0b2d-4a7c-9e3f-1d2c3b4a5f60
printf '%s\n' "${boot_id}" > "${MICRODAP_BOOT_ID_FILE}"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

run() {
    rc=0
    out=$("${keep}" "$@" 2>&1) || rc=$?
}

rm -f "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 1 ]] || fail "check with no file exit ${rc}, expected 1"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 0 ]] || fail "check with the boot id exit ${rc}"
[[ -z "${out}" ]] || fail "check printed '${out}'"

printf '  %s \r\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 0 ]] || fail "check with padded marker exit ${rc}"

printf '%s' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 0 ]] || fail "check with no trailing newline exit ${rc}"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
printf '%s' "${boot_id}" > "${MICRODAP_BOOT_ID_FILE}"
run check
[[ "${rc}" == 0 ]] || fail "check with untrimmed boot id file exit ${rc}"
printf '%s\n' "${boot_id}" > "${MICRODAP_BOOT_ID_FILE}"

printf '%s\n' "00000000-0000-0000-0000-000000000000" > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 1 ]] || fail "check with another id exit ${rc}, expected 1"

: > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 1 ]] || fail "check with empty file exit ${rc}, expected 1"

printf '%s\n%s\n' "${boot_id}" "trailing" > "${MICRODAP_KEEP_FILE}"
run check
[[ "${rc}" == 0 ]] || fail "check with a second line exit ${rc}"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
rm -f "${MICRODAP_BOOT_ID_FILE}"
run check
[[ "${rc}" == 1 ]] || fail "check with no boot id file exit ${rc}, expected 1"
printf '%s\n' "${boot_id}" > "${MICRODAP_BOOT_ID_FILE}"

printf '%s\n' "${boot_id}" > "${MICRODAP_KEEP_FILE}"
run clear
[[ "${rc}" == 0 ]] || fail "clear exit ${rc}"
[[ ! -e "${MICRODAP_KEEP_FILE}" ]] || fail "clear left the marker"

run clear
[[ "${rc}" == 0 ]] || fail "clear with no file exit ${rc}"

run check
[[ "${rc}" == 1 ]] || fail "check after clear exit ${rc}, expected 1"

for verb in bogus "" "check;id" "clear --help" write; do
    run "${verb}"
    [[ "${rc}" == 1 ]] || fail "verb '${verb}' exit ${rc}, expected 1"
done

run
[[ "${rc}" == 1 ]] || fail "no verb exit ${rc}, expected 1"

run check clear
[[ "${rc}" == 1 ]] || fail "two args exit ${rc}, expected 1"

if (( failures )); then
    echo "keep-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "keep-test: ok"
