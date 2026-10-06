#!/usr/bin/bash
set -eu

pins="glibc libstdc++ libgcc qt6-qtbase qt6-qtdeclarative gstreamer1 SDL3 pipewire taglib libxcb"
negativo17=https://negativo17.org/repos/fedora-multimedia.repo
negativo17_file=/etc/yum.repos.d/negativo17-fedora-multimedia.repo
koji=https://kojipkgs.fedoraproject.org/packages

snapshot() { rpm -q --qf '%{NAME} %{EVR}\n' ${pins}; }
evr() { rpm -q --qf '%{EVR}' "$1"; }

dnf_install() {
    dnf5 -y install --setopt=install_weak_deps=False "$@"
}

koji_install() {
    local runtime=$1 src ver rel dir name
    shift
    src=$(rpm -q --qf '%{SOURCERPM}' "${runtime}")
    src=${src%.src.rpm}
    rel=${src##*-}
    src=${src%-*}
    ver=${src##*-}
    src=${src%-*}
    dir=$(mktemp -d)
    for name in "$@"; do
        curl -fsSL -o "${dir}/${name}.rpm" \
            "${koji}/${src}/${ver}/${rel}/aarch64/${name}-${ver}-${rel}.aarch64.rpm"
    done
    dnf_install "${dir}"/*.rpm
    rm -rf "${dir}"
}

pinned() {
    local runtime=$1 vendor name specs=
    shift
    for name in "$@"; do
        specs="${specs} ${name}-$(evr "${runtime}")"
    done
    vendor=$(rpm -q --qf '%{VENDOR}' "${runtime}")
    if [[ "${vendor}" != "Fedora Project" ]]; then
        curl -fsSL -o "${negativo17_file}" "${negativo17}"
        dnf_install ${specs}
        rm -f "${negativo17_file}"
        return 0
    fi
    dnf_install ${specs} || koji_install "${runtime}" "$@"
}

before=$(snapshot)

dnf_install cmake ninja-build meson patch squashfs-tools jq git unzip ccache rpm cpio

pinned libstdc++ gcc gcc-c++ cpp libstdc++-devel
pinned glibc glibc-devel
for name in qt6-qtbase qt6-qtdeclarative gstreamer1 SDL3 pipewire taglib libxcb; do
    pinned "${name}" "${name}-devel"
done

after=$(snapshot)
if [[ "${before}" != "${after}" ]]; then
    echo "pinned runtime packages changed during the toolchain install:" >&2
    diff <(echo "${before}") <(echo "${after}") >&2 || true
    exit 1
fi
dnf5 clean all
