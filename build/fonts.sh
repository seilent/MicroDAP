#!/usr/bin/bash
set -eu

url=https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip
sha=3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423
license=https://raw.githubusercontent.com/microsoft/Selawik/89362e8/LICENSE.txt
license_sha=77b7c2506d4efb22e09c8ccf10159f4956eab3ef7c007fef95de136bcf45300c
faces="selawkl selawksl selawk selawksb selawkb"

cache=$1
dist=$2
zip="${cache}/Selawik_Release.zip"

mkdir -p "${cache}"
if ! echo "${sha}  ${zip}" | sha256sum -c --status 2>/dev/null; then
    curl -fsSL -o "${zip}.part" "${url}"
    echo "${sha}  ${zip}.part" | sha256sum -c --status || { echo "fonts.sh: Selawik zip sha256 mismatch" >&2; exit 1; }
    mv "${zip}.part" "${zip}"
fi
if ! echo "${license_sha}  ${cache}/LICENSE.txt" | sha256sum -c --status 2>/dev/null; then
    curl -fsSL -o "${cache}/LICENSE.txt" "${license}"
    echo "${license_sha}  ${cache}/LICENSE.txt" | sha256sum -c --status || { echo "fonts.sh: Selawik license sha256 mismatch" >&2; exit 1; }
fi

mkdir -p "${dist}/usr/share/fonts/casdap" "${dist}/usr/share/licenses/casdap"
for face in ${faces}; do
    unzip -o -q -j "${zip}" "${face}.ttf" -d "${dist}/usr/share/fonts/casdap"
done
cp "${cache}/LICENSE.txt" "${dist}/usr/share/licenses/casdap/Selawik.txt"
