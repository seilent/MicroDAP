cmake_minimum_required(VERSION 3.28)
project(microdap LANGUAGES CXX)

include(GNUInstallDirs)

enable_testing()
add_subdirectory(player)

install(PROGRAMS
    data/libexec/microdap-bt
    data/libexec/microdap-keep
    data/libexec/microdap-radio
    data/libexec/microdap-stopped
    data/libexec/microdap-usb-audio
    data/libexec/microdap-wait-x
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/microdap)

install(DIRECTORY data/microos/ DESTINATION lib/microos)
install(DIRECTORY data/systemd/ DESTINATION lib/systemd)
install(FILES data/udev/70-microdap-usb-audio.rules DESTINATION lib/udev/rules.d)
install(FILES data/microset/schema.d/50-microdap.json DESTINATION ${CMAKE_INSTALL_DATADIR}/microset/schema.d)
install(FILES data/gamescope-session-plus/sessions.d/microdap DESTINATION ${CMAKE_INSTALL_DATADIR}/gamescope-session-plus/sessions.d)
install(FILES data/wayland-sessions/microdap.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
