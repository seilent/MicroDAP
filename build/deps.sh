#!/usr/bin/bash
set -eu

dnf5 -y install --setopt=install_weak_deps=False \
    cmake ninja-build gcc-c++ ccache git curl unzip findutils diffutils \
    qt6-qtbase-devel qt6-qtdeclarative-devel alsa-lib-devel \
    gstreamer1-devel gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugin-libav \
    taglib-devel libxcb-devel pulseaudio-libs-devel \
    dbus-daemon dbus-tools python3 python3-dbus python3-gobject systemd util-linux fontconfig
dnf5 clean all
