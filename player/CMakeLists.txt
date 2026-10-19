cmake_minimum_required(VERSION 3.28)
project(microdap-player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest DBus Sql Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-controller-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-mainloop-glib)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
find_package(MicroSet REQUIRED CONFIG)

qt_standard_project_setup(REQUIRES 6.11)

add_library(microdapfiio STATIC
    src/core/Fiio.cpp
    src/core/Fiio.h
    src/core/FiioCli.cpp
    src/core/FiioCli.h
)
set_target_properties(microdapfiio PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(microdapfiio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

add_executable(microdap-fiio src/app/fiio_main.cpp)
set_target_properties(microdap-fiio PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(microdap-fiio PRIVATE microdapfiio)
install(TARGETS microdap-fiio RUNTIME DESTINATION libexec/microdap)

add_library(microdaprows STATIC
    src/core/PathFit.h
    src/core/SettingsRows.cpp
    src/core/SettingsRows.h
)
set_target_properties(microdaprows PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(microdaprows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(microdaprows PUBLIC Qt6::Core)

add_library(microdapcore STATIC
    src/core/AccentControl.cpp
    src/core/AccentControl.h
    src/core/AlsaVolume.cpp
    src/core/AlsaVolume.h
    src/core/ArtColor.cpp
    src/core/ArtColor.h
    src/core/Biquad.cpp
    src/core/Biquad.h
    src/core/BluezModel.cpp
    src/core/BluezModel.h
    src/core/Crossfade.cpp
    src/core/Crossfade.h
    src/core/Cue.cpp
    src/core/Cue.h
    src/core/DacEq.cpp
    src/core/DacEq.h
    src/core/DbusPad.cpp
    src/core/DbusPad.h
    src/core/Deck.cpp
    src/core/Deck.h
    src/core/DeviceReservation.cpp
    src/core/DeviceReservation.h
    src/core/Eq.cpp
    src/core/Eq.h
    src/core/FakeBluezModel.cpp
    src/core/FakeBluezModel.h
    src/core/Favorites.cpp
    src/core/Favorites.h
    src/core/FiioWorker.cpp
    src/core/FiioWorker.h
    src/core/GuideKey.cpp
    src/core/GuideKey.h
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
    src/core/LinkProfile.cpp
    src/core/LinkProfile.h
    src/core/LinkProto.h
    src/core/LinkServer.cpp
    src/core/LinkServer.h
    src/core/Lyrics.cpp
    src/core/Lyrics.h
    src/core/Models.cpp
    src/core/Models.h
    src/core/Mpris.cpp
    src/core/Mpris.h
    src/core/OutputControl.cpp
    src/core/OutputControl.h
    src/core/OutputMode.cpp
    src/core/OutputMode.h
    src/core/OutputPorts.h
    src/core/OutputWatch.cpp
    src/core/OutputWatch.h
    src/core/OverlayBridge.cpp
    src/core/OverlayBridge.h
    src/core/PhoneArt.cpp
    src/core/PhoneArt.h
    src/core/PadSource.h
    src/core/PlayThrough.cpp
    src/core/PlayThrough.h
    src/core/Player.cpp
    src/core/Player.h
    src/core/PlaylistStore.cpp
    src/core/PlaylistStore.h
    src/core/PowerBridge.cpp
    src/core/PowerBridge.h
    src/core/Queue.cpp
    src/core/Queue.h
    src/core/ReplayGain.cpp
    src/core/ReplayGain.h
    src/core/RgbControl.cpp
    src/core/RgbControl.h
    src/core/Rotation.cpp
    src/core/Rotation.h
    src/core/Scanner.cpp
    src/core/Scanner.h
    src/core/SectionKey.cpp
    src/core/SectionKey.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/SleepInhibitor.cpp
    src/core/SleepInhibitor.h
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
    src/core/WirePlumberRestart.cpp
    src/core/WirePlumberRestart.h
)
target_include_directories(microdapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_definitions(microdapcore PRIVATE MICRODAP_VERSION="${PROJECT_VERSION}")
target_link_libraries(microdapcore PUBLIC
    flatdap_link
    microdapfiio
    microdaprows
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Sql
    PkgConfig::GST
    PkgConfig::TAGLIB
    PkgConfig::PULSE
    PkgConfig::ALSA
    MicroSet::MicroSet
)

install(FILES
    dbus/net.seilent.MicroDap.Player.xml
    dbus/org.mpris.MediaPlayer2.xml
    dbus/org.mpris.MediaPlayer2.Player.xml
    DESTINATION share/dbus-1/interfaces
)

set(microdap_qml
    qml/MicroDap/Main.qml
    qml/MicroDap/Ui.qml
    qml/MicroDap/Motion.qml
    qml/MicroDap/Turnstile.qml
    qml/MicroDap/PageStack.qml
    qml/MicroDap/Page.qml
    qml/MicroDap/PadRouter.qml
    qml/MicroDap/FocusBar.qml
    qml/MicroDap/FocusList.qml
    qml/MicroDap/PressScale.qml
    qml/MicroDap/TouchScroll.qml
    qml/MicroDap/TouchLayer.qml
    qml/MicroDap/Specimen.qml
    qml/MicroDap/MotionShot.qml
    qml/MicroDap/components/Glyph.qml
    qml/MicroDap/components/Ring.qml
    qml/MicroDap/components/CroppedHeader.qml
    qml/MicroDap/components/PivotStrip.qml
    qml/MicroDap/components/ListRow.qml
    qml/MicroDap/components/Tile.qml
    qml/MicroDap/components/LetterTile.qml
    qml/MicroDap/components/Backdrop.qml
    qml/MicroDap/components/PageSurface.qml
    qml/MicroDap/components/StatusStrip.qml
    qml/MicroDap/components/Sheet.qml
    qml/MicroDap/components/Dialog.qml
    qml/MicroDap/components/Toast.qml
    qml/MicroDap/components/VolumeOverlay.qml
    qml/MicroDap/components/JumpLetter.qml
    qml/MicroDap/components/Keyboard.qml
    qml/MicroDap/components/LyricsColumn.qml
    qml/MicroDap/components/PairingPanel.qml
    qml/MicroDap/GlyphPaths.js
    qml/MicroDap/EqFormat.js
    qml/MicroDap/ComponentsShot.qml
    qml/MicroDap/OverlaysShot.qml
    qml/MicroDap/VolumeShot.qml
    qml/MicroDap/HomeShot.qml
    qml/MicroDap/screens/Home.qml
    qml/MicroDap/screens/Quickplay.qml
    qml/MicroDap/screens/Bluetooth.qml
    qml/MicroDap/screens/PhoneAppPairing.qml
    qml/MicroDap/screens/SwitchPrompt.qml
    qml/MicroDap/screens/MusicHub.qml
    qml/MicroDap/screens/NowPlaying.qml
    qml/MicroDap/screens/QueueScreen.qml
    qml/MicroDap/screens/ArtistDetail.qml
    qml/MicroDap/screens/AlbumDetail.qml
    qml/MicroDap/screens/SettingsScreen.qml
    qml/MicroDap/screens/JumpGrid.qml
    qml/MicroDap/screens/PlaylistsScreen.qml
    qml/MicroDap/screens/PlaylistView.qml
    qml/MicroDap/screens/HistoryScreen.qml
    qml/MicroDap/screens/EqScreen.qml
    qml/MicroDap/screens/Saver.qml
    qml/MicroDap/screens/PowerMenu.qml
    qml/MicroDap/OverlayLayer.qml
    qml/MicroDap/BluetoothShot.qml
    qml/MicroDap/PhoneAppShot.qml
    qml/MicroDap/PromptShot.qml
    qml/MicroDap/PowerShot.qml
    qml/MicroDap/SaverShot.qml
    qml/MicroDap/HubShot.qml
    qml/MicroDap/NowPlayingShot.qml
    qml/MicroDap/QueueShot.qml
    qml/MicroDap/DetailShot.qml
    qml/MicroDap/SettingsShot.qml
    qml/MicroDap/PlaylistsShot.qml
    qml/MicroDap/HistoryShot.qml
    qml/MicroDap/EqShot.qml
    qml/MicroDap/KeyboardShot.qml
)
foreach(file IN LISTS microdap_qml)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(qml/MicroDap/Ui.qml qml/MicroDap/Motion.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(microdapui STATIC)
qt_add_qml_module(microdapui
    URI MicroDap
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${microdap_qml}
    SOURCES
        src/core/KeyboardModel.cpp
        src/core/KeyboardModel.h
        src/ui/Env.cpp
        src/ui/Env.h
)
target_include_directories(microdapui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
target_link_libraries(microdapui PUBLIC microdaprows Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microdap-player
    src/app/main.cpp
    src/app/Gamescope.cpp
    src/app/Gamescope.h
    src/app/Screenshot.cpp
    src/app/Screenshot.h
)
target_link_libraries(microdap-player PRIVATE microdapcore microdapuiplugin Qt6::Gui Qt6::Quick PkgConfig::XCB)

install(TARGETS microdap-player RUNTIME DESTINATION bin)

enable_testing()
include(tests/env.cmake)
add_subdirectory(tests)
