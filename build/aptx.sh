#!/usr/bin/bash
set -euo pipefail

APTX_LIB_RPM=libfreeaptx-0.2.2-3.fc44.aarch64.rpm
APTX_LIB_SHA=55ecd53e8d5c34be64fc93841c31db76cee4f6b1d6ca677a27fe6f9ccf342b6b
APTX_CODEC_RPM=pipewire-codec-aptx-1.6.7-1.fc44.aarch64.rpm
APTX_CODEC_SHA=69dadfdb63d334f6f420e4fe9aee5e91f76764a32be114249f243ac61d4b5f5f

lib_url=https://download1.rpmfusion.org/free/fedora/releases/44/Everything/aarch64/os/Packages/l/
codec_url=https://download1.rpmfusion.org/free/fedora/updates/44/aarch64/p/

cache=$1
dist=$2

for var in APTX_LIB_RPM APTX_LIB_SHA APTX_CODEC_RPM APTX_CODEC_SHA; do
    [[ -n "${!var}" ]] || { echo "aptx.sh: ${var} is empty, fill the pin at the top of build/aptx.sh" >&2; exit 1; }
done

fetch() {
    local url=$1 sha=$2 file=$3
    if echo "${sha}  ${file}" | sha256sum -c --status 2>/dev/null; then
        return 0
    fi
    curl -fsSL -o "${file}.part" "${url}"
    echo "${sha}  ${file}.part" | sha256sum -c --status || { echo "aptx.sh: sha256 mismatch for $(basename "${file}")" >&2; exit 1; }
    mv "${file}.part" "${file}"
}

extract() {
    local rpm=$1 dest=$2
    shift 2
    mkdir -p "${dest}"
    rpm2cpio "${rpm}" | (cd "${dest}" && cpio -idm --quiet "$@")
}

license() {
    local root=$1 name=$2 file
    file=$(find "${root}/usr/share/licenses" -type f 2>/dev/null | sort | head -n 1)
    [[ -n "${file}" ]] || return 1
    cp "${file}" "${dist}/usr/share/licenses/casdap/${name}.txt"
}

mkdir -p "${cache}"
fetch "${lib_url}${APTX_LIB_RPM}" "${APTX_LIB_SHA}" "${cache}/${APTX_LIB_RPM}"
fetch "${codec_url}${APTX_CODEC_RPM}" "${APTX_CODEC_SHA}" "${cache}/${APTX_CODEC_RPM}"

work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT

extract "${cache}/${APTX_LIB_RPM}" "${work}/lib" './usr/lib64/libfreeaptx.so.0*' './usr/share/licenses/*'
extract "${cache}/${APTX_CODEC_RPM}" "${work}/codec" './usr/lib64/spa-0.2/bluez5/libspa-codec-bluez5-aptx.so' './usr/share/licenses/*'

ls "${work}"/lib/usr/lib64/libfreeaptx.so.0* >/dev/null 2>&1 || { echo "aptx.sh: libfreeaptx.so.0* missing in ${APTX_LIB_RPM}" >&2; exit 1; }
[[ -f "${work}/codec/usr/lib64/spa-0.2/bluez5/libspa-codec-bluez5-aptx.so" ]] || { echo "aptx.sh: libspa-codec-bluez5-aptx.so missing in ${APTX_CODEC_RPM}" >&2; exit 1; }

mkdir -p "${dist}/usr/lib64/spa-0.2/bluez5" "${dist}/usr/share/licenses/casdap"
cp -a "${work}"/lib/usr/lib64/libfreeaptx.so.0* "${dist}/usr/lib64/"
cp -a "${work}/codec/usr/lib64/spa-0.2/bluez5/libspa-codec-bluez5-aptx.so" "${dist}/usr/lib64/spa-0.2/bluez5/"
license "${work}/lib" libfreeaptx || { echo "aptx.sh: no license file in ${APTX_LIB_RPM}" >&2; exit 1; }
license "${work}/codec" pipewire-codec-aptx || true
