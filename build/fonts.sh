#!/usr/bin/bash
set -eu

url=https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip
sha=3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423
faces="selawkl selawksl selawk selawksb selawkb"

cache=$1
zip="${cache}/Selawik_Release.zip"

mkdir -p "${cache}"
if ! echo "${sha}  ${zip}" | sha256sum -c --status 2>/dev/null; then
    curl -fsSL -o "${zip}.part" "${url}"
    echo "${sha}  ${zip}.part" | sha256sum -c --status || { echo "fonts.sh: Selawik zip sha256 mismatch" >&2; exit 1; }
    mv "${zip}.part" "${zip}"
fi

for face in ${faces}; do
    [[ -f "${cache}/${face}.ttf" ]] || unzip -o -q -j "${zip}" "${face}.ttf" -d "${cache}"
done
