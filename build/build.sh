#!/usr/bin/bash
set -eu

cd "$(dirname "$0")/.."

. build/base.env

version="${CASDAP_VERSION:-unknown}"
root="${CASDAP_BUILDER_ROOT:-}"
dist=dist
share="${dist}/usr/share/casdap"

fail() { echo "build.sh: $*" >&2; exit 1; }

os_value() { sed -n "s/^$1=//p" "${root}/usr/lib/os-release" | tr -d '"'; }

[[ "$(os_value IMAGE_VERSION)" == "${IMAGE_VERSION}" ]] || fail "builder IMAGE_VERSION $(os_value IMAGE_VERSION) differs from base.env ${IMAGE_VERSION}"
[[ "$(os_value VERSION_ID)" == "${VERSION_ID}" ]] || fail "builder VERSION_ID $(os_value VERSION_ID) differs from base.env ${VERSION_ID}"

rm -rf "${dist}"
mkdir -p "${share}"
cp -a sysext/. "${dist}/"

: > "${share}/shadow-base.sha256"
for p in $(find patches -type f -name '*.patch' | sort); do
    path=${p#patches/*/}
    path=/${path%.patch}
    base=$(sed -n '1s|^--- a/\([^[:space:]]*\).*|/\1|p' "${p}")
    [[ -n "${base}" ]] || fail "${p} has no --- a/ header"
    [[ "$(grep -c '^+++ ' "${p}")" == 1 ]] || fail "${p} must have exactly one +++ header"
    [[ "$(sed -n '2s|^+++ b/\([^[:space:]]*\).*|/\1|p' "${p}")" == "${path}" ]] || fail "${p} does not patch ${path}"
    [[ -f "${root}${base}" ]] || fail "base file ${base} is not in the builder image"
    mkdir -p "${dist}$(dirname "${path}")"
    cp -a "${root}${base}" "${dist}${path}"
    echo "$(sha256sum "${root}${base}" | cut -d' ' -f1)  ${base}" >> "${share}/shadow-base.sha256"
    patch --no-backup-if-mismatch "${dist}${path}" < "${p}"
done

build/fonts.sh build/cache/fonts "${dist}"
bash build/aptx.sh build/cache/aptx "${dist}"

if [[ -f player/CMakeLists.txt ]]; then
    export CCACHE_DIR="${PWD}/build/cache/ccache"
    cmake -S player -B build/player -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
    cmake --build build/player
    DESTDIR="${PWD}/${dist}" cmake --install build/player
fi

echo "${version}" > "${share}/version"
{
    echo "BASE_IMAGE=${BASE_IMAGE}"
    echo "IMAGE_VERSION=${IMAGE_VERSION}"
    echo "VERSION_ID=${VERSION_ID}"
    echo "CASDAP_VERSION=${version}"
    echo
    rpm -qa | sort
} > "${share}/build-manifest"

if [[ "${CASDAP_IMAGE:-0}" == 1 ]]; then
    raw="casdap-${version}-armada${VERSION_ID}-arm64.raw"
    rm -f "${raw}"
    mksquashfs "${dist}" "${raw}" -comp zstd -noappend -all-root -quiet
    echo "${raw}"
fi
