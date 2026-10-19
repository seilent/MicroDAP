string(REPLACE "." "\\." version_regex "${PROJECT_VERSION}")
set(shots ${CMAKE_CURRENT_BINARY_DIR}/shots)
file(MAKE_DIRECTORY ${shots})

add_test(NAME version COMMAND microdap-player --version)
set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^microdap-player ${version_regex}")
microdap_test_env(version)

add_test(NAME screenshot-blank
    COMMAND microdap-player --screenshot blank --size 640x960 --out ${shots}/blank-640x960.png
)
set_tests_properties(screenshot-blank PROPERTIES FIXTURES_SETUP shot-blank)
microdap_test_env(screenshot-blank)

qt_add_executable(tst_shot core/tst_shot.cpp)
target_compile_definitions(tst_shot PRIVATE
    MICRODAP_SHOT_PNG="${shots}/blank-640x960.png"
    MICRODAP_SHOT_UPRIGHT="${shots}/settings-960x640.png"
    MICRODAP_SHOT_TURNED="${shots}/settings-landscape-640x960.png"
)
target_link_libraries(tst_shot PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME screenshot-check COMMAND tst_shot blank)
set_tests_properties(screenshot-check PROPERTIES FIXTURES_REQUIRED shot-blank)
microdap_test_env(screenshot-check)

set(fixtures ${CMAKE_CURRENT_BINARY_DIR}/fixtures/music)
set(formats ${CMAKE_CURRENT_BINARY_DIR}/fixtures/formats)
add_executable(mkfixtures core/mkfixtures.cpp)
set_target_properties(mkfixtures PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(mkfixtures PRIVATE PkgConfig::GST PkgConfig::TAGLIB)
add_test(NAME library-fixtures COMMAND mkfixtures ${fixtures} ${formats})
set_tests_properties(library-fixtures PROPERTIES FIXTURES_SETUP library-fixtures)

qt_add_executable(tst_library core/tst_library.cpp)
target_compile_definitions(tst_library PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}" MICRODAP_FIXTURE_FORMATS="${formats}")
target_link_libraries(tst_library PRIVATE microdapcore Qt6::Test)
add_test(NAME library COMMAND tst_library)
set_tests_properties(library PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(library)

qt_add_executable(tst_player core/tst_player.cpp)
target_compile_definitions(tst_player PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}" MICRODAP_FIXTURE_FORMATS="${formats}")
target_link_libraries(tst_player PRIVATE microdapcore Qt6::Test)
add_test(NAME player COMMAND tst_player)
set_tests_properties(player PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(player)
set_property(TEST player APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false"
             "MICRODAP_EXCLUSIVE_SINK=fakesink sync=false")

qt_add_executable(tst_playlists core/tst_playlists.cpp)
target_compile_definitions(tst_playlists PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_playlists PRIVATE microdapcore Qt6::Test)
add_test(NAME playlists COMMAND tst_playlists)
set_tests_properties(playlists PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(playlists)

qt_add_executable(tst_lyrics core/tst_lyrics.cpp)
target_compile_definitions(tst_lyrics PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}" MICRODAP_FIXTURE_FORMATS="${formats}")
target_link_libraries(tst_lyrics PRIVATE microdapcore Qt6::Test)
add_test(NAME lyrics COMMAND tst_lyrics)
set_tests_properties(lyrics PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(lyrics)

qt_add_executable(tst_state core/tst_state.cpp)
target_link_libraries(tst_state PRIVATE microdapcore Qt6::Test)
add_test(NAME state COMMAND tst_state)
microdap_test_env(state)
set_property(TEST state APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false"
             "MICRODAP_EXCLUSIVE_SINK=fakesink sync=false")

qt_add_executable(tst_scale_queue core/tst_scale_queue.cpp)
target_link_libraries(tst_scale_queue PRIVATE microdapcore Qt6::Test)
add_test(NAME scale-queue COMMAND tst_scale_queue)
microdap_test_env(scale-queue)
set_property(TEST scale-queue APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false")

qt_add_executable(tst_mpris core/tst_mpris.cpp)
target_compile_definitions(tst_mpris PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_mpris PRIVATE microdapcore Qt6::DBus Qt6::Test)
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_test(NAME mpris COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_mpris>)
else()
    add_test(NAME mpris COMMAND tst_mpris)
endif()
set_tests_properties(mpris PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(mpris)
set_property(TEST mpris APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=true")

qt_add_executable(tst_power core/tst_power.cpp)
target_compile_definitions(tst_power PRIVATE MICRODAP_FAKE_LOGIN1="${CMAKE_CURRENT_SOURCE_DIR}/fake_login1.py"
                                             MICRODAP_FAKE_POWERD="${CMAKE_CURRENT_SOURCE_DIR}/fake_powerd.py"
                                             MICRODAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_power PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME power COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_power>)
else()
    add_test(NAME power COMMAND tst_power)
endif()
set_tests_properties(power PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(power)
set_property(TEST power APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false")

qt_add_executable(tst_audioinfo core/tst_audioinfo.cpp)
target_link_libraries(tst_audioinfo PRIVATE microdapcore Qt6::Test)
add_test(NAME audioinfo COMMAND tst_audioinfo)
microdap_test_env(audioinfo)

qt_add_executable(tst_rgb core/tst_rgb.cpp)
target_compile_definitions(tst_rgb PRIVATE MICRODAP_FAKE_MICROSETD="${CMAKE_CURRENT_SOURCE_DIR}/fake_microsetd.py")
target_link_libraries(tst_rgb PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME rgb COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_rgb>)
else()
    add_test(NAME rgb COMMAND tst_rgb)
endif()
microdap_test_env(rgb)

qt_add_executable(tst_settings core/tst_settings.cpp)
target_compile_definitions(tst_settings PRIVATE MICRODAP_FAKE_MICROSETD="${CMAKE_CURRENT_SOURCE_DIR}/fake_microsetd.py")
target_link_libraries(tst_settings PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME settings COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_settings>)
else()
    add_test(NAME settings COMMAND tst_settings)
endif()
microdap_test_env(settings)

qt_add_executable(tst_output core/tst_output.cpp)
target_link_libraries(tst_output PRIVATE microdapcore Qt6::Test)
add_test(NAME output COMMAND tst_output)
microdap_test_env(output)

qt_add_executable(tst_guide core/tst_guide.cpp)
target_link_libraries(tst_guide PRIVATE microdapcore Qt6::Test)
add_test(NAME guide COMMAND tst_guide)
microdap_test_env(guide)

qt_add_executable(tst_outputs core/tst_outputs.cpp)
target_link_libraries(tst_outputs PRIVATE microdapcore Qt6::Gui Qt6::Test)
add_test(NAME outputs COMMAND tst_outputs)
microdap_test_env(outputs)

qt_add_executable(tst_reservation core/tst_reservation.cpp)
target_compile_definitions(tst_reservation PRIVATE MICRODAP_FAKE_RESERVE="${CMAKE_CURRENT_SOURCE_DIR}/fake_reserve.py")
target_link_libraries(tst_reservation PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME reservation COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_reservation>)
    microdap_test_env(reservation)
endif()

qt_add_executable(tst_volumekeys core/tst_volumekeys.cpp)
target_link_libraries(tst_volumekeys PRIVATE microdapcore Qt6::Test)
add_test(NAME volumekeys COMMAND tst_volumekeys)
microdap_test_env(volumekeys)

qt_add_executable(tst_outputmode core/tst_outputmode.cpp)
target_link_libraries(tst_outputmode PRIVATE microdapcore Qt6::Test)
add_test(NAME outputmode COMMAND tst_outputmode)
microdap_test_env(outputmode)

set(volume_nodes ${CMAKE_CURRENT_BINARY_DIR}/volume-nodes)
file(MAKE_DIRECTORY ${volume_nodes})
add_test(NAME volumekeys-screenshot
    COMMAND microdap-player --screenshot blank --size 640x960 --out ${shots}/volumekeys-blank-640x960.png
)
set_tests_properties(volumekeys-screenshot PROPERTIES FAIL_REGULAR_EXPRESSION "microdap volume:")
microdap_test_env(volumekeys-screenshot)
set_property(TEST volumekeys-screenshot APPEND PROPERTY ENVIRONMENT
    "MICRODAP_DEV_INPUT=${volume_nodes}" "MICRODAP_SYS_INPUT=${volume_nodes}")

find_program(TIMEOUT_BIN timeout)
if(DBUS_RUN_SESSION AND TIMEOUT_BIN)
    add_test(NAME volumekeys-overlay
        COMMAND ${DBUS_RUN_SESSION} -- ${TIMEOUT_BIN} 5 $<TARGET_FILE:microdap-player> --overlay
    )
    set_tests_properties(volumekeys-overlay PROPERTIES
        PASS_REGULAR_EXPRESSION "overlay mode needs the xcb platform"
        FAIL_REGULAR_EXPRESSION "microdap volume:"
    )
    microdap_test_env(volumekeys-overlay)
    set_property(TEST volumekeys-overlay APPEND PROPERTY ENVIRONMENT
        "MICRODAP_DEV_INPUT=${volume_nodes}" "MICRODAP_SYS_INPUT=${volume_nodes}" "MICRODAP_AUDIO_SINK=fakesink sync=false")
endif()

qt_add_executable(tst_artcolor core/tst_artcolor.cpp)
target_link_libraries(tst_artcolor PRIVATE microdapcore Qt6::Test)
add_test(NAME artcolor COMMAND tst_artcolor)
microdap_test_env(artcolor)

qt_add_executable(tst_scale core/tst_scale.cpp)
target_link_libraries(tst_scale PRIVATE microdapcore Qt6::Test)
add_test(NAME scale COMMAND tst_scale)
microdap_test_env(scale)

qt_add_executable(tst_eq core/tst_eq.cpp)
target_link_libraries(tst_eq PRIVATE microdapcore Qt6::Test)
add_test(NAME eq COMMAND tst_eq)
microdap_test_env(eq)

qt_add_executable(tst_biquad core/tst_biquad.cpp)
target_link_libraries(tst_biquad PRIVATE microdapcore Qt6::Test)
add_test(NAME biquad COMMAND tst_biquad)
microdap_test_env(biquad)

qt_add_executable(tst_linkproto core/tst_linkproto.cpp)
target_compile_definitions(tst_linkproto PRIVATE MICRODAP_FLATDAP_LINK="${PROJECT_SOURCE_DIR}/../third_party/flatdap/link")
target_link_libraries(tst_linkproto PRIVATE flatdap_link Qt6::Test)
add_test(NAME linkproto COMMAND tst_linkproto)
microdap_test_env(linkproto)

qt_add_executable(tst_link core/tst_link.cpp)
target_link_libraries(tst_link PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME link COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_link>)
else()
    add_test(NAME link COMMAND tst_link)
endif()
microdap_test_env(link)
set_property(TEST link APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false")

qt_add_executable(tst_replaygain core/tst_replaygain.cpp)
target_link_libraries(tst_replaygain PRIVATE microdapcore Qt6::Test)
add_test(NAME replaygain COMMAND tst_replaygain)
microdap_test_env(replaygain)

qt_add_executable(tst_crossfade core/tst_crossfade.cpp)
target_link_libraries(tst_crossfade PRIVATE microdapcore Qt6::Test)
add_test(NAME crossfade COMMAND tst_crossfade)
microdap_test_env(crossfade)

qt_add_executable(tst_fiio core/tst_fiio.cpp)
target_link_libraries(tst_fiio PRIVATE microdapcore Qt6::Test)
add_test(NAME fiio COMMAND tst_fiio)
microdap_test_env(fiio)

qt_add_executable(tst_daceq core/tst_daceq.cpp)
target_link_libraries(tst_daceq PRIVATE microdapcore Qt6::Test)
add_test(NAME daceq COMMAND tst_daceq)
microdap_test_env(daceq)

set(fiio_cli_script [=[
set -eu
bin=$1
stub=$2
root=$(dirname "$MICRODAP_SYS_ROOT")
mkdir -p "$root/empty"
out=$(MICRODAP_SYS_ROOT="$root/empty" "$bin" reset 2>&1)
case $out in
*"microdap-fiio: no supported dac"*) ;;
*) printf '%s\n' "$out"; exit 1 ;;
esac
dev=$root/fixture/devices/3-1
mkdir -p "$dev/3-1:1.0/ep_05" "$dev/3-1:1.0/ep_86" "$root/fixture/bus/usb/devices" "$MICRODAP_RUN_DIR/systemd"
printf '2972\n' >"$dev/idVendor"
printf '0149\n' >"$dev/idProduct"
printf '03\n' >"$dev/3-1:1.0/bInterfaceClass"
ln -sfn "$dev" "$root/fixture/bus/usb/devices/3-1"
: >"$MICRODAP_RUN_DIR/systemd/private"
out=$(MICRODAP_SYS_ROOT="$root/fixture" MICRODAP_SYSTEMCTL_BIN="$stub" "$bin" reset 2>&1)
case $out in
*"microdap-fiio: microdap session active, skipped"*) ;;
*) printf '%s\n' "$out"; exit 1 ;;
esac
]=])
add_test(NAME fiio-cli COMMAND sh -c "${fiio_cli_script}" sh $<TARGET_FILE:microdap-fiio>
         ${CMAKE_CURRENT_SOURCE_DIR}/stub_systemctl.sh)
microdap_test_env(fiio-cli)

qt_add_executable(tst_playthrough core/tst_playthrough.cpp)
target_link_libraries(tst_playthrough PRIVATE microdapcore Qt6::Test)
add_test(NAME playthrough COMMAND tst_playthrough)
microdap_test_env(playthrough)

qt_add_executable(tst_hubmodels core/tst_hubmodels.cpp)
target_link_libraries(tst_hubmodels PRIVATE microdapcore Qt6::Test)
add_test(NAME hubmodels COMMAND tst_hubmodels)
microdap_test_env(hubmodels)

qt_add_executable(tst_bluez core/tst_bluez.cpp)
target_compile_definitions(tst_bluez PRIVATE MICRODAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_bluez PRIVATE microdapcore Qt6::Test)
add_test(NAME bluez COMMAND tst_bluez)
set_tests_properties(bluez PROPERTIES FIXTURES_REQUIRED library-fixtures)
microdap_test_env(bluez)
set_property(TEST bluez APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=true")

qt_add_executable(tst_systembluez core/tst_systembluez.cpp)
target_link_libraries(tst_systembluez PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME systembluez COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_systembluez>)
else()
    add_test(NAME systembluez COMMAND tst_systembluez)
endif()
microdap_test_env(systembluez)

qt_add_executable(tst_phoneart core/tst_phoneart.cpp)
target_link_libraries(tst_phoneart PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME phoneart COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_phoneart>)
else()
    add_test(NAME phoneart COMMAND tst_phoneart)
endif()
microdap_test_env(phoneart)

qt_add_executable(tst_dbuspad core/tst_dbuspad.cpp)
target_compile_definitions(tst_dbuspad PRIVATE
    MICRODAP_FAKE_INPUTPLUMBER="${CMAKE_CURRENT_SOURCE_DIR}/fake_inputplumber.py"
    MICRODAP_FAKE_ARBITER="${CMAKE_CURRENT_SOURCE_DIR}/fake_arbiter.py"
)
target_link_libraries(tst_dbuspad PRIVATE microdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME dbuspad COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_dbuspad>)
else()
    add_test(NAME dbuspad COMMAND tst_dbuspad)
endif()
microdap_test_env(dbuspad)

qt_add_executable(tst_overlay core/tst_overlay.cpp)
target_compile_definitions(tst_overlay PRIVATE
    MICRODAP_FAKE_INPUTPLUMBER="${CMAKE_CURRENT_SOURCE_DIR}/fake_inputplumber.py"
    MICRODAP_FAKE_ARBITER="${CMAKE_CURRENT_SOURCE_DIR}/fake_arbiter.py"
    MICRODAP_FAKE_LOGIN1="${CMAKE_CURRENT_SOURCE_DIR}/fake_login1.py"
)
target_link_libraries(tst_overlay PRIVATE microdapcore microdapuiplugin Qt6::DBus Qt6::Gui Qt6::Quick Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME overlay COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_overlay>)
else()
    add_test(NAME overlay COMMAND tst_overlay)
endif()
microdap_test_env(overlay)
set_property(TEST overlay APPEND PROPERTY ENVIRONMENT "MICRODAP_AUDIO_SINK=fakesink sync=false")

set(MICRODAP_TEST_FONT_DIR ${PROJECT_SOURCE_DIR}/../build/cache/fonts CACHE PATH "Selawik TTF directory for the qml tests and specimen shots")

qt_add_executable(tst_qml qml/tst_qml.cpp)
target_compile_definitions(tst_qml PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(tst_qml PRIVATE microdapuiplugin Qt6::Gui Qt6::Quick Qt6::QuickTest)
file(GLOB qml_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_*.qml)
foreach(file IN LISTS qml_tests)
    get_filename_component(stem ${file} NAME_WE)
    string(REGEX REPLACE "^tst_" "" suite ${stem})
    add_test(NAME qml-${suite} COMMAND tst_qml -input ${file})
    microdap_test_env(qml-${suite})
    set_property(TEST qml-${suite} APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-specimen-portrait
    COMMAND microdap-player --screenshot specimen --size 640x960 --out ${shots}/specimen-640x960.png
)
microdap_test_env(screenshot-specimen-portrait)
set_property(TEST screenshot-specimen-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-specimen-landscape
    COMMAND microdap-player --screenshot specimen --size 1920x1080 --out ${shots}/specimen-1920x1080.png
)
microdap_test_env(screenshot-specimen-landscape)
set_property(TEST screenshot-specimen-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-motion
    COMMAND microdap-player --screenshot motion --size 640x960 --angle 40 --out ${shots}/motion-640x960.png
)
microdap_test_env(screenshot-motion)
set_property(TEST screenshot-motion APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-components-portrait
    COMMAND microdap-player --screenshot components --size 640x960 --out ${shots}/components-640x960.png
)
microdap_test_env(screenshot-components-portrait)
set_property(TEST screenshot-components-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-components-landscape
    COMMAND microdap-player --screenshot components --size 1920x1080 --out ${shots}/components-1920x1080.png
)
microdap_test_env(screenshot-components-landscape)
set_property(TEST screenshot-components-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-overlays-portrait
    COMMAND microdap-player --screenshot overlays --size 640x960 --out ${shots}/overlays-640x960.png
)
microdap_test_env(screenshot-overlays-portrait)
set_property(TEST screenshot-overlays-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-overlays-landscape
    COMMAND microdap-player --screenshot overlays --size 1920x1080 --out ${shots}/overlays-1920x1080.png
)
microdap_test_env(screenshot-overlays-landscape)
set_property(TEST screenshot-overlays-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-volume-portrait
    COMMAND microdap-player --screenshot volume --size 640x960 --out ${shots}/volume-640x960.png
)
microdap_test_env(screenshot-volume-portrait)
set_property(TEST screenshot-volume-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-volume-landscape
    COMMAND microdap-player --screenshot volume --size 1920x1080 --out ${shots}/volume-1920x1080.png
)
microdap_test_env(screenshot-volume-landscape)
set_property(TEST screenshot-volume-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-portrait
    COMMAND microdap-player --screenshot home --size 640x960 --out ${shots}/home-640x960.png
)
microdap_test_env(screenshot-home-portrait)
set_property(TEST screenshot-home-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-landscape
    COMMAND microdap-player --screenshot home --size 1920x1080 --out ${shots}/home-1920x1080.png
)
microdap_test_env(screenshot-home-landscape)
set_property(TEST screenshot-home-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-bluez-portrait
    COMMAND microdap-player --screenshot home --size 640x960 --bluez connected --out ${shots}/home-bluez-640x960.png
)
microdap_test_env(screenshot-home-bluez-portrait)
set_property(TEST screenshot-home-bluez-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-bluez-landscape
    COMMAND microdap-player --screenshot home --size 1920x1080 --bluez connected --out ${shots}/home-bluez-1920x1080.png
)
microdap_test_env(screenshot-home-bluez-landscape)
set_property(TEST screenshot-home-bluez-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-quickplay-portrait
    COMMAND microdap-player --screenshot quickplay --size 640x960 --out ${shots}/quickplay-640x960.png
)
microdap_test_env(screenshot-quickplay-portrait)
set_property(TEST screenshot-quickplay-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-quickplay-landscape
    COMMAND microdap-player --screenshot quickplay --size 1920x1080 --out ${shots}/quickplay-1920x1080.png
)
microdap_test_env(screenshot-quickplay-landscape)
set_property(TEST screenshot-quickplay-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-connected
    COMMAND microdap-player --screenshot bluetooth --size 640x960 --bluez connected --out ${shots}/bluetooth-connected-640x960.png
)
microdap_test_env(screenshot-bluetooth-connected)
set_property(TEST screenshot-bluetooth-connected APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-active
    COMMAND microdap-player --screenshot bluetooth --size 640x960 --bluez active --out ${shots}/bluetooth-active-640x960.png
)
microdap_test_env(screenshot-bluetooth-active)
set_property(TEST screenshot-bluetooth-active APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-pairing
    COMMAND microdap-player --screenshot bluetooth --size 640x960 --bluez pairing --out ${shots}/bluetooth-pairing-640x960.png
)
microdap_test_env(screenshot-bluetooth-pairing)
set_property(TEST screenshot-bluetooth-pairing APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-prompt-portrait
    COMMAND microdap-player --screenshot prompt --size 640x960 --out ${shots}/prompt-640x960.png
)
microdap_test_env(screenshot-prompt-portrait)
set_property(TEST screenshot-prompt-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-prompt-landscape
    COMMAND microdap-player --screenshot prompt --size 1920x1080 --out ${shots}/prompt-1920x1080.png
)
microdap_test_env(screenshot-prompt-landscape)
set_property(TEST screenshot-prompt-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

foreach(pivot IN ITEMS artists albums songs genres playlists folders)
    add_test(NAME screenshot-hub-${pivot}-portrait
        COMMAND microdap-player --screenshot hub --pivot ${pivot} --size 640x960 --library ${fixtures} --out ${shots}/hub-${pivot}-640x960.png
    )
    set_tests_properties(screenshot-hub-${pivot}-portrait PROPERTIES FIXTURES_REQUIRED library-fixtures)
    microdap_test_env(screenshot-hub-${pivot}-portrait)
    set_property(TEST screenshot-hub-${pivot}-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-hub-${pivot}-landscape
        COMMAND microdap-player --screenshot hub --pivot ${pivot} --size 1920x1080 --library ${fixtures} --out ${shots}/hub-${pivot}-1920x1080.png
    )
    set_tests_properties(screenshot-hub-${pivot}-landscape PROPERTIES FIXTURES_REQUIRED library-fixtures)
    microdap_test_env(screenshot-hub-${pivot}-landscape)
    set_property(TEST screenshot-hub-${pivot}-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
endforeach()

foreach(size IN ITEMS 640x960 1920x1080)
    add_test(NAME screenshot-hub-folders-open-${size}
        COMMAND microdap-player --screenshot hub --pivot folders --state folder-open --size ${size} --library ${fixtures} --out ${shots}/hub-folders-open-${size}.png
    )
    set_tests_properties(screenshot-hub-folders-open-${size} PROPERTIES FIXTURES_REQUIRED library-fixtures)
    microdap_test_env(screenshot-hub-folders-open-${size})
    set_property(TEST screenshot-hub-folders-open-${size} APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-nowplaying-portrait
    COMMAND microdap-player --screenshot nowplaying --size 640x960 --out ${shots}/nowplaying-640x960.png
)
microdap_test_env(screenshot-nowplaying-portrait)
set_property(TEST screenshot-nowplaying-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-landscape
    COMMAND microdap-player --screenshot nowplaying --size 1920x1080 --out ${shots}/nowplaying-1920x1080.png
)
microdap_test_env(screenshot-nowplaying-landscape)
set_property(TEST screenshot-nowplaying-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-bluez-portrait
    COMMAND microdap-player --screenshot nowplaying --size 640x960 --bluez active --out ${shots}/nowplaying-bluez-640x960.png
)
microdap_test_env(screenshot-nowplaying-bluez-portrait)
set_property(TEST screenshot-nowplaying-bluez-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-bluez-landscape
    COMMAND microdap-player --screenshot nowplaying --size 1920x1080 --bluez active --out ${shots}/nowplaying-bluez-1920x1080.png
)
microdap_test_env(screenshot-nowplaying-bluez-landscape)
set_property(TEST screenshot-nowplaying-bluez-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-queue-portrait
    COMMAND microdap-player --screenshot queue --size 640x960 --out ${shots}/queue-640x960.png
)
microdap_test_env(screenshot-queue-portrait)
set_property(TEST screenshot-queue-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-queue-landscape
    COMMAND microdap-player --screenshot queue --size 1920x1080 --out ${shots}/queue-1920x1080.png
)
microdap_test_env(screenshot-queue-landscape)
set_property(TEST screenshot-queue-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

foreach(screen IN ITEMS artist album settings playlists history eq power saver saver-dim)
    add_test(NAME screenshot-${screen}-portrait
        COMMAND microdap-player --screenshot ${screen} --size 640x960 --out ${shots}/${screen}-640x960.png
    )
    microdap_test_env(screenshot-${screen}-portrait)
    set_property(TEST screenshot-${screen}-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-${screen}-landscape
        COMMAND microdap-player --screenshot ${screen} --size 1920x1080 --out ${shots}/${screen}-1920x1080.png
    )
    microdap_test_env(screenshot-${screen}-landscape)
    set_property(TEST screenshot-${screen}-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
endforeach()

foreach(shot IN ITEMS settings:pivot-library settings:pivot-display settings:pivot-power settings:pivot-system
        settings:accent-edit settings:saver-sheet settings:sample-rate-edit settings:touch settings:touch-edit
        settings:boot settings:boot-edit settings:boot-missing settings:about settings:about-available
        settings:about-missing settings:play-through settings:play-through-sheet settings:resume-sheet
        power:power-none power:power-long nowplaying:seek components:tracknum album:art-collapse album:art-dock
        album:art-docked nowplaying:clock-24h nowplaying:clock-12h nowplaying:percent nowplaying:percent-low
        home:quickplay-off keyboard:browse keyboard:kb-empty keyboard:kb-long
        keyboard:kb-petal keyboard:kb-petal-corner keyboard:kb-shift keyboard:kb-symbols keyboard:kb-discard
        keyboard:kb-number-freq keyboard:kb-number-q keyboard:kb-number-gain keyboard:kb-refused)
    string(REPLACE ":" ";" parts ${shot})
    list(GET parts 0 screen)
    list(GET parts 1 state)
    foreach(size IN ITEMS 640x960 960x640)
        add_test(NAME screenshot-${screen}-${state}-${size}
            COMMAND microdap-player --screenshot ${screen} --state ${state} --size ${size} --out ${shots}/${screen}-${state}-${size}.png
        )
        microdap_test_env(screenshot-${screen}-${state}-${size})
        set_property(TEST screenshot-${screen}-${state}-${size} APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
    endforeach()
endforeach()

foreach(screen IN ITEMS home settings)
    add_test(NAME screenshot-${screen}-overlay-portrait
        COMMAND microdap-player --screenshot ${screen} --overlay --size 640x960 --out ${shots}/${screen}-overlay-640x960.png
    )
    microdap_test_env(screenshot-${screen}-overlay-portrait)
    set_property(TEST screenshot-${screen}-overlay-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-${screen}-overlay-landscape
        COMMAND microdap-player --screenshot ${screen} --overlay --size 960x640 --out ${shots}/${screen}-overlay-960x640.png
    )
    microdap_test_env(screenshot-${screen}-overlay-landscape)
    set_property(TEST screenshot-${screen}-overlay-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-settings-upright
    COMMAND microdap-player --screenshot settings --size 960x640 --out ${shots}/settings-960x640.png
)
set_tests_properties(screenshot-settings-upright PROPERTIES FIXTURES_SETUP shot-orientation)
microdap_test_env(screenshot-settings-upright)
set_property(TEST screenshot-settings-upright APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-settings-turned
    COMMAND microdap-player --screenshot settings --size 640x960 --orientation landscape --out ${shots}/settings-landscape-640x960.png
)
set_tests_properties(screenshot-settings-turned PROPERTIES FIXTURES_SETUP shot-orientation)
microdap_test_env(screenshot-settings-turned)
set_property(TEST screenshot-settings-turned APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-orientation COMMAND tst_shot orientation)
set_tests_properties(screenshot-orientation PROPERTIES FIXTURES_REQUIRED shot-orientation)
microdap_test_env(screenshot-orientation)

add_test(NAME screenshot-jump-portrait
    COMMAND microdap-player --screenshot jump --pivot artists --size 640x960 --out ${shots}/jump-640x960.png
)
microdap_test_env(screenshot-jump-portrait)
set_property(TEST screenshot-jump-portrait APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")

add_test(NAME screenshot-jump-landscape
    COMMAND microdap-player --screenshot jump --pivot artists --size 1920x1080 --out ${shots}/jump-1920x1080.png
)
microdap_test_env(screenshot-jump-landscape)
set_property(TEST screenshot-jump-landscape APPEND PROPERTY ENVIRONMENT "MICRODAP_FONT_DIR=${MICRODAP_TEST_FONT_DIR}")
