string(REPLACE "." "\\." version_regex "${PROJECT_VERSION}")
set(shots ${CMAKE_CURRENT_BINARY_DIR}/shots)
file(MAKE_DIRECTORY ${shots})

add_test(NAME version COMMAND casdap-player --version)
set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^casdap-player ${version_regex}")
casdap_test_env(version)

add_test(NAME screenshot-blank
    COMMAND casdap-player --screenshot blank --size 640x960 --out ${shots}/blank-640x960.png
)
set_tests_properties(screenshot-blank PROPERTIES FIXTURES_SETUP shot-blank)
casdap_test_env(screenshot-blank)

qt_add_executable(tst_shot core/tst_shot.cpp)
target_compile_definitions(tst_shot PRIVATE CASDAP_SHOT_PNG="${shots}/blank-640x960.png")
target_link_libraries(tst_shot PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME screenshot-check COMMAND tst_shot)
set_tests_properties(screenshot-check PROPERTIES FIXTURES_REQUIRED shot-blank)
casdap_test_env(screenshot-check)

set(fixtures ${CMAKE_CURRENT_BINARY_DIR}/fixtures/music)
add_executable(mkfixtures core/mkfixtures.cpp)
set_target_properties(mkfixtures PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(mkfixtures PRIVATE PkgConfig::GST PkgConfig::TAGLIB)
add_test(NAME library-fixtures COMMAND mkfixtures ${fixtures})
set_tests_properties(library-fixtures PROPERTIES FIXTURES_SETUP library-fixtures)

qt_add_executable(tst_library core/tst_library.cpp)
target_compile_definitions(tst_library PRIVATE CASDAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_library PRIVATE casdapcore Qt6::Test)
add_test(NAME library COMMAND tst_library)
set_tests_properties(library PROPERTIES FIXTURES_REQUIRED library-fixtures)
casdap_test_env(library)

qt_add_executable(tst_player core/tst_player.cpp)
target_compile_definitions(tst_player PRIVATE CASDAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_player PRIVATE casdapcore Qt6::Test)
add_test(NAME player COMMAND tst_player)
set_tests_properties(player PROPERTIES FIXTURES_REQUIRED library-fixtures)
casdap_test_env(player)
set_property(TEST player APPEND PROPERTY ENVIRONMENT "CASDAP_AUDIO_SINK=fakesink sync=false")

qt_add_executable(tst_mpris core/tst_mpris.cpp)
target_compile_definitions(tst_mpris PRIVATE CASDAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_mpris PRIVATE casdapcore Qt6::DBus Qt6::Test)
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_test(NAME mpris COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_mpris>)
else()
    add_test(NAME mpris COMMAND tst_mpris)
endif()
set_tests_properties(mpris PROPERTIES FIXTURES_REQUIRED library-fixtures)
casdap_test_env(mpris)
set_property(TEST mpris APPEND PROPERTY ENVIRONMENT "CASDAP_AUDIO_SINK=fakesink sync=true")

qt_add_executable(tst_pad core/tst_pad.cpp)
target_link_libraries(tst_pad PRIVATE casdapcore Qt6::Test)
add_test(NAME pad COMMAND tst_pad)
casdap_test_env(pad)
set_property(TEST pad APPEND PROPERTY ENVIRONMENT "SDL_JOYSTICK_HIDAPI=0")

qt_add_executable(tst_power core/tst_power.cpp)
target_compile_definitions(tst_power PRIVATE CASDAP_FAKE_LOGIN1="${CMAKE_CURRENT_SOURCE_DIR}/fake_login1.py")
target_link_libraries(tst_power PRIVATE casdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME power COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_power>)
else()
    add_test(NAME power COMMAND tst_power)
endif()
casdap_test_env(power)
set_property(TEST power APPEND PROPERTY ENVIRONMENT "CASDAP_AUDIO_SINK=fakesink sync=false")

qt_add_executable(tst_audioinfo core/tst_audioinfo.cpp)
target_link_libraries(tst_audioinfo PRIVATE casdapcore Qt6::Test)
add_test(NAME audioinfo COMMAND tst_audioinfo)
casdap_test_env(audioinfo)

qt_add_executable(tst_rgb core/tst_rgb.cpp)
target_compile_definitions(tst_rgb PRIVATE CASDAP_UI_QML="${CMAKE_SOURCE_DIR}/qml/Casdap/Ui.qml")
target_link_libraries(tst_rgb PRIVATE casdapcore Qt6::Test)
add_test(NAME rgb COMMAND tst_rgb)
casdap_test_env(rgb)

qt_add_executable(tst_output core/tst_output.cpp)
target_link_libraries(tst_output PRIVATE casdapcore Qt6::Test)
add_test(NAME output COMMAND tst_output)
casdap_test_env(output)

qt_add_executable(tst_codec core/tst_codec.cpp)
target_link_libraries(tst_codec PRIVATE casdapcore Qt6::Test)
add_test(NAME codec COMMAND tst_codec)
casdap_test_env(codec)

qt_add_executable(tst_bluez core/tst_bluez.cpp)
target_compile_definitions(tst_bluez PRIVATE CASDAP_FIXTURE_MUSIC="${fixtures}")
target_link_libraries(tst_bluez PRIVATE casdapcore Qt6::Test)
add_test(NAME bluez COMMAND tst_bluez)
set_tests_properties(bluez PROPERTIES FIXTURES_REQUIRED library-fixtures)
casdap_test_env(bluez)
set_property(TEST bluez APPEND PROPERTY ENVIRONMENT "CASDAP_AUDIO_SINK=fakesink sync=true")

qt_add_executable(tst_systembluez core/tst_systembluez.cpp)
target_link_libraries(tst_systembluez PRIVATE casdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME systembluez COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_systembluez>)
else()
    add_test(NAME systembluez COMMAND tst_systembluez)
endif()
casdap_test_env(systembluez)

qt_add_executable(tst_dbuspad core/tst_dbuspad.cpp)
target_compile_definitions(tst_dbuspad PRIVATE CASDAP_FAKE_INPUTPLUMBER="${CMAKE_CURRENT_SOURCE_DIR}/fake_inputplumber.py")
target_link_libraries(tst_dbuspad PRIVATE casdapcore Qt6::DBus Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME dbuspad COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_dbuspad>)
else()
    add_test(NAME dbuspad COMMAND tst_dbuspad)
endif()
casdap_test_env(dbuspad)

qt_add_executable(tst_overlay core/tst_overlay.cpp)
target_compile_definitions(tst_overlay PRIVATE
    CASDAP_FAKE_INPUTPLUMBER="${CMAKE_CURRENT_SOURCE_DIR}/fake_inputplumber.py"
    CASDAP_FAKE_LOGIN1="${CMAKE_CURRENT_SOURCE_DIR}/fake_login1.py"
)
target_link_libraries(tst_overlay PRIVATE casdapcore casdapuiplugin Qt6::DBus Qt6::Gui Qt6::Quick Qt6::Test)
if(DBUS_RUN_SESSION)
    add_test(NAME overlay COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_overlay>)
else()
    add_test(NAME overlay COMMAND tst_overlay)
endif()
casdap_test_env(overlay)
set_property(TEST overlay APPEND PROPERTY ENVIRONMENT "CASDAP_AUDIO_SINK=fakesink sync=false")

set(CASDAP_TEST_FONT_DIR ${CMAKE_SOURCE_DIR}/../dist/usr/share/fonts/casdap CACHE PATH "Selawik TTF directory for the qml tests and specimen shots")

qt_add_executable(tst_qml qml/tst_qml.cpp)
target_compile_definitions(tst_qml PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(tst_qml PRIVATE casdapuiplugin Qt6::Gui Qt6::Quick Qt6::QuickTest)
file(GLOB qml_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_*.qml)
foreach(file IN LISTS qml_tests)
    get_filename_component(stem ${file} NAME_WE)
    string(REGEX REPLACE "^tst_" "" suite ${stem})
    add_test(NAME qml-${suite} COMMAND tst_qml -input ${file})
    casdap_test_env(qml-${suite})
    set_property(TEST qml-${suite} APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-specimen-portrait
    COMMAND casdap-player --screenshot specimen --size 640x960 --out ${shots}/specimen-640x960.png
)
casdap_test_env(screenshot-specimen-portrait)
set_property(TEST screenshot-specimen-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-specimen-landscape
    COMMAND casdap-player --screenshot specimen --size 1920x1080 --out ${shots}/specimen-1920x1080.png
)
casdap_test_env(screenshot-specimen-landscape)
set_property(TEST screenshot-specimen-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-motion
    COMMAND casdap-player --screenshot motion --size 640x960 --angle 40 --out ${shots}/motion-640x960.png
)
casdap_test_env(screenshot-motion)
set_property(TEST screenshot-motion APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-components-portrait
    COMMAND casdap-player --screenshot components --size 640x960 --out ${shots}/components-640x960.png
)
casdap_test_env(screenshot-components-portrait)
set_property(TEST screenshot-components-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-components-landscape
    COMMAND casdap-player --screenshot components --size 1920x1080 --out ${shots}/components-1920x1080.png
)
casdap_test_env(screenshot-components-landscape)
set_property(TEST screenshot-components-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-overlays-portrait
    COMMAND casdap-player --screenshot overlays --size 640x960 --out ${shots}/overlays-640x960.png
)
casdap_test_env(screenshot-overlays-portrait)
set_property(TEST screenshot-overlays-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-overlays-landscape
    COMMAND casdap-player --screenshot overlays --size 1920x1080 --out ${shots}/overlays-1920x1080.png
)
casdap_test_env(screenshot-overlays-landscape)
set_property(TEST screenshot-overlays-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-volume-portrait
    COMMAND casdap-player --screenshot volume --size 640x960 --out ${shots}/volume-640x960.png
)
casdap_test_env(screenshot-volume-portrait)
set_property(TEST screenshot-volume-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-volume-landscape
    COMMAND casdap-player --screenshot volume --size 1920x1080 --out ${shots}/volume-1920x1080.png
)
casdap_test_env(screenshot-volume-landscape)
set_property(TEST screenshot-volume-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-portrait
    COMMAND casdap-player --screenshot home --size 640x960 --out ${shots}/home-640x960.png
)
casdap_test_env(screenshot-home-portrait)
set_property(TEST screenshot-home-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-landscape
    COMMAND casdap-player --screenshot home --size 1920x1080 --out ${shots}/home-1920x1080.png
)
casdap_test_env(screenshot-home-landscape)
set_property(TEST screenshot-home-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-bluez-portrait
    COMMAND casdap-player --screenshot home --size 640x960 --bluez connected --out ${shots}/home-bluez-640x960.png
)
casdap_test_env(screenshot-home-bluez-portrait)
set_property(TEST screenshot-home-bluez-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-home-bluez-landscape
    COMMAND casdap-player --screenshot home --size 1920x1080 --bluez connected --out ${shots}/home-bluez-1920x1080.png
)
casdap_test_env(screenshot-home-bluez-landscape)
set_property(TEST screenshot-home-bluez-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-quickplay-portrait
    COMMAND casdap-player --screenshot quickplay --size 640x960 --out ${shots}/quickplay-640x960.png
)
casdap_test_env(screenshot-quickplay-portrait)
set_property(TEST screenshot-quickplay-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-quickplay-landscape
    COMMAND casdap-player --screenshot quickplay --size 1920x1080 --out ${shots}/quickplay-1920x1080.png
)
casdap_test_env(screenshot-quickplay-landscape)
set_property(TEST screenshot-quickplay-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-connected
    COMMAND casdap-player --screenshot bluetooth --size 640x960 --bluez connected --out ${shots}/bluetooth-connected-640x960.png
)
casdap_test_env(screenshot-bluetooth-connected)
set_property(TEST screenshot-bluetooth-connected APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-active
    COMMAND casdap-player --screenshot bluetooth --size 640x960 --bluez active --out ${shots}/bluetooth-active-640x960.png
)
casdap_test_env(screenshot-bluetooth-active)
set_property(TEST screenshot-bluetooth-active APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-bluetooth-pairing
    COMMAND casdap-player --screenshot bluetooth --size 640x960 --bluez pairing --out ${shots}/bluetooth-pairing-640x960.png
)
casdap_test_env(screenshot-bluetooth-pairing)
set_property(TEST screenshot-bluetooth-pairing APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-prompt-portrait
    COMMAND casdap-player --screenshot prompt --size 640x960 --out ${shots}/prompt-640x960.png
)
casdap_test_env(screenshot-prompt-portrait)
set_property(TEST screenshot-prompt-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-prompt-landscape
    COMMAND casdap-player --screenshot prompt --size 1920x1080 --out ${shots}/prompt-1920x1080.png
)
casdap_test_env(screenshot-prompt-landscape)
set_property(TEST screenshot-prompt-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

foreach(pivot IN ITEMS artists albums songs genres playlists)
    add_test(NAME screenshot-hub-${pivot}-portrait
        COMMAND casdap-player --screenshot hub --pivot ${pivot} --size 640x960 --library ${fixtures} --out ${shots}/hub-${pivot}-640x960.png
    )
    set_tests_properties(screenshot-hub-${pivot}-portrait PROPERTIES FIXTURES_REQUIRED library-fixtures)
    casdap_test_env(screenshot-hub-${pivot}-portrait)
    set_property(TEST screenshot-hub-${pivot}-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-hub-${pivot}-landscape
        COMMAND casdap-player --screenshot hub --pivot ${pivot} --size 1920x1080 --library ${fixtures} --out ${shots}/hub-${pivot}-1920x1080.png
    )
    set_tests_properties(screenshot-hub-${pivot}-landscape PROPERTIES FIXTURES_REQUIRED library-fixtures)
    casdap_test_env(screenshot-hub-${pivot}-landscape)
    set_property(TEST screenshot-hub-${pivot}-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-nowplaying-portrait
    COMMAND casdap-player --screenshot nowplaying --size 640x960 --out ${shots}/nowplaying-640x960.png
)
casdap_test_env(screenshot-nowplaying-portrait)
set_property(TEST screenshot-nowplaying-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-landscape
    COMMAND casdap-player --screenshot nowplaying --size 1920x1080 --out ${shots}/nowplaying-1920x1080.png
)
casdap_test_env(screenshot-nowplaying-landscape)
set_property(TEST screenshot-nowplaying-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-bluez-portrait
    COMMAND casdap-player --screenshot nowplaying --size 640x960 --bluez active --out ${shots}/nowplaying-bluez-640x960.png
)
casdap_test_env(screenshot-nowplaying-bluez-portrait)
set_property(TEST screenshot-nowplaying-bluez-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-nowplaying-bluez-landscape
    COMMAND casdap-player --screenshot nowplaying --size 1920x1080 --bluez active --out ${shots}/nowplaying-bluez-1920x1080.png
)
casdap_test_env(screenshot-nowplaying-bluez-landscape)
set_property(TEST screenshot-nowplaying-bluez-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-queue-portrait
    COMMAND casdap-player --screenshot queue --size 640x960 --out ${shots}/queue-640x960.png
)
casdap_test_env(screenshot-queue-portrait)
set_property(TEST screenshot-queue-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-queue-landscape
    COMMAND casdap-player --screenshot queue --size 1920x1080 --out ${shots}/queue-1920x1080.png
)
casdap_test_env(screenshot-queue-landscape)
set_property(TEST screenshot-queue-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

foreach(screen IN ITEMS artist album settings playlists history power saver saver-dim)
    add_test(NAME screenshot-${screen}-portrait
        COMMAND casdap-player --screenshot ${screen} --size 640x960 --out ${shots}/${screen}-640x960.png
    )
    casdap_test_env(screenshot-${screen}-portrait)
    set_property(TEST screenshot-${screen}-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-${screen}-landscape
        COMMAND casdap-player --screenshot ${screen} --size 1920x1080 --out ${shots}/${screen}-1920x1080.png
    )
    casdap_test_env(screenshot-${screen}-landscape)
    set_property(TEST screenshot-${screen}-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")
endforeach()

foreach(screen IN ITEMS home settings)
    add_test(NAME screenshot-${screen}-overlay-portrait
        COMMAND casdap-player --screenshot ${screen} --overlay --size 640x960 --out ${shots}/${screen}-overlay-640x960.png
    )
    casdap_test_env(screenshot-${screen}-overlay-portrait)
    set_property(TEST screenshot-${screen}-overlay-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

    add_test(NAME screenshot-${screen}-overlay-landscape
        COMMAND casdap-player --screenshot ${screen} --overlay --size 960x640 --out ${shots}/${screen}-overlay-960x640.png
    )
    casdap_test_env(screenshot-${screen}-overlay-landscape)
    set_property(TEST screenshot-${screen}-overlay-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")
endforeach()

add_test(NAME screenshot-jump-portrait
    COMMAND casdap-player --screenshot jump --pivot artists --size 640x960 --out ${shots}/jump-640x960.png
)
casdap_test_env(screenshot-jump-portrait)
set_property(TEST screenshot-jump-portrait APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")

add_test(NAME screenshot-jump-landscape
    COMMAND casdap-player --screenshot jump --pivot artists --size 1920x1080 --out ${shots}/jump-1920x1080.png
)
casdap_test_env(screenshot-jump-landscape)
set_property(TEST screenshot-jump-landscape APPEND PROPERTY ENVIRONMENT "CASDAP_FONT_DIR=${CASDAP_TEST_FONT_DIR}")
