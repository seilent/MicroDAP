cmake_minimum_required(VERSION 3.28)
project(casdap-player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest DBus Sql Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-mainloop-glib)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
find_package(SDL3 REQUIRED CONFIG)

qt_standard_project_setup(REQUIRES 6.11)

add_library(casdapcore STATIC
    src/core/AccentControl.cpp
    src/core/AccentControl.h
    src/core/AlsaVolume.cpp
    src/core/AlsaVolume.h
    src/core/ArtColor.cpp
    src/core/ArtColor.h
    src/core/BluezModel.cpp
    src/core/BluezModel.h
    src/core/CodecFragment.cpp
    src/core/CodecFragment.h
    src/core/DbusPad.cpp
    src/core/DbusPad.h
    src/core/DeviceReservation.cpp
    src/core/DeviceReservation.h
    src/core/Eq.cpp
    src/core/Eq.h
    src/core/FakeBluezModel.cpp
    src/core/FakeBluezModel.h
    src/core/GuideChord.cpp
    src/core/GuideChord.h
    src/core/HubModels.cpp
    src/core/HubModels.h
    src/core/Intercept.cpp
    src/core/Intercept.h
    src/core/Keep.cpp
    src/core/AudioInfo.cpp
    src/core/AudioInfo.h
    src/core/Keep.h
    src/core/Library.cpp
    src/core/Library.h
    src/core/LibraryWatcher.cpp
    src/core/LibraryWatcher.h
    src/core/Models.cpp
    src/core/Models.h
    src/core/Mpris.cpp
    src/core/Mpris.h
    src/core/OutputControl.cpp
    src/core/OutputControl.h
    src/core/OutputPorts.h
    src/core/OutputWatch.cpp
    src/core/OutputWatch.h
    src/core/OverlayBridge.cpp
    src/core/OverlayBridge.h
    src/core/Pad.cpp
    src/core/Pad.h
    src/core/PadSource.h
    src/core/Player.cpp
    src/core/Player.h
    src/core/PowerBridge.cpp
    src/core/PowerBridge.h
    src/core/QrtrLink.cpp
    src/core/QrtrLink.h
    src/core/Queue.cpp
    src/core/Queue.h
    src/core/RgbControl.cpp
    src/core/RgbControl.h
    src/core/Scanner.cpp
    src/core/Scanner.h
    src/core/SectionKey.cpp
    src/core/SectionKey.h
    src/core/SensorRotation.cpp
    src/core/SensorRotation.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/SleepInhibitor.cpp
    src/core/SleepInhibitor.h
    src/core/SscLink.h
    src/core/SscRotation.cpp
    src/core/SscRotation.h
    src/core/SscWire.cpp
    src/core/SscWire.h
    src/core/State.cpp
    src/core/State.h
    src/core/StatusModel.cpp
    src/core/StatusModel.h
    src/core/SystemBluezModel.cpp
    src/core/SystemBluezModel.h
    src/core/Version.cpp
    src/core/Version.h
    src/core/VolumeKeys.cpp
    src/core/VolumeKeys.h
)
target_include_directories(casdapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_definitions(casdapcore PRIVATE CASDAP_VERSION="${PROJECT_VERSION}")
target_link_libraries(casdapcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Sql
    PkgConfig::GST
    PkgConfig::TAGLIB
    PkgConfig::PULSE
    PkgConfig::ALSA
    SDL3::SDL3
)

install(FILES
    dbus/org.casdap.Player.xml
    dbus/org.mpris.MediaPlayer2.xml
    dbus/org.mpris.MediaPlayer2.Player.xml
    DESTINATION share/dbus-1/interfaces
)

set(casdap_qml
    qml/Casdap/Main.qml
    qml/Casdap/Ui.qml
    qml/Casdap/Motion.qml
    qml/Casdap/Turnstile.qml
    qml/Casdap/PageStack.qml
    qml/Casdap/Page.qml
    qml/Casdap/PadRouter.qml
    qml/Casdap/FocusBar.qml
    qml/Casdap/FocusList.qml
    qml/Casdap/PressScale.qml
    qml/Casdap/TouchScroll.qml
    qml/Casdap/TouchLayer.qml
    qml/Casdap/Specimen.qml
    qml/Casdap/MotionShot.qml
    qml/Casdap/components/Glyph.qml
    qml/Casdap/components/Ring.qml
    qml/Casdap/components/CroppedHeader.qml
    qml/Casdap/components/PivotStrip.qml
    qml/Casdap/components/ListRow.qml
    qml/Casdap/components/Tile.qml
    qml/Casdap/components/LetterTile.qml
    qml/Casdap/components/Backdrop.qml
    qml/Casdap/components/PageSurface.qml
    qml/Casdap/components/StatusStrip.qml
    qml/Casdap/components/Sheet.qml
    qml/Casdap/components/Dialog.qml
    qml/Casdap/components/Toast.qml
    qml/Casdap/components/VolumeOverlay.qml
    qml/Casdap/components/JumpLetter.qml
    qml/Casdap/GlyphPaths.js
    qml/Casdap/EqFormat.js
    qml/Casdap/ComponentsShot.qml
    qml/Casdap/OverlaysShot.qml
    qml/Casdap/VolumeShot.qml
    qml/Casdap/HomeShot.qml
    qml/Casdap/screens/Home.qml
    qml/Casdap/screens/Quickplay.qml
    qml/Casdap/screens/Bluetooth.qml
    qml/Casdap/screens/SwitchPrompt.qml
    qml/Casdap/screens/MusicHub.qml
    qml/Casdap/screens/NowPlaying.qml
    qml/Casdap/screens/QueueScreen.qml
    qml/Casdap/screens/ArtistDetail.qml
    qml/Casdap/screens/AlbumDetail.qml
    qml/Casdap/screens/SettingsScreen.qml
    qml/Casdap/screens/JumpGrid.qml
    qml/Casdap/screens/PlaylistsScreen.qml
    qml/Casdap/screens/HistoryScreen.qml
    qml/Casdap/screens/EqScreen.qml
    qml/Casdap/screens/Saver.qml
    qml/Casdap/screens/PowerMenu.qml
    qml/Casdap/OverlayLayer.qml
    qml/Casdap/BluetoothShot.qml
    qml/Casdap/PromptShot.qml
    qml/Casdap/PowerShot.qml
    qml/Casdap/SaverShot.qml
    qml/Casdap/HubShot.qml
    qml/Casdap/NowPlayingShot.qml
    qml/Casdap/QueueShot.qml
    qml/Casdap/DetailShot.qml
    qml/Casdap/SettingsShot.qml
    qml/Casdap/PlaylistsShot.qml
    qml/Casdap/HistoryShot.qml
    qml/Casdap/EqShot.qml
)
foreach(file IN LISTS casdap_qml)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(qml/Casdap/Ui.qml qml/Casdap/Motion.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(casdapui STATIC)
qt_add_qml_module(casdapui
    URI Casdap
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${casdap_qml}
    SOURCES
        src/ui/Env.cpp
        src/ui/Env.h
)
target_include_directories(casdapui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
target_link_libraries(casdapui PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(casdap-player
    src/app/main.cpp
    src/app/Gamescope.cpp
    src/app/Gamescope.h
    src/app/Screenshot.cpp
    src/app/Screenshot.h
)
target_link_libraries(casdap-player PRIVATE casdapcore casdapuiplugin Qt6::Gui Qt6::Quick PkgConfig::XCB)

install(TARGETS casdap-player RUNTIME DESTINATION bin)

enable_testing()
include(tests/env.cmake)
add_subdirectory(tests)
