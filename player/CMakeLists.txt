cmake_minimum_required(VERSION 3.28)
project(casdap-player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest DBus Sql Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
find_package(SDL3 REQUIRED CONFIG)

qt_standard_project_setup(REQUIRES 6.11)

add_library(casdapcore STATIC
    src/core/Library.cpp
    src/core/Library.h
    src/core/Models.cpp
    src/core/Models.h
    src/core/Scanner.cpp
    src/core/Scanner.h
    src/core/Version.cpp
    src/core/Version.h
)
target_include_directories(casdapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_definitions(casdapcore PRIVATE CASDAP_VERSION="${PROJECT_VERSION}")
target_link_libraries(casdapcore PUBLIC
    Qt6::Core
    Qt6::DBus
    Qt6::Sql
    PkgConfig::GST
    PkgConfig::TAGLIB
    SDL3::SDL3
)

qt_add_executable(casdap-player
    src/app/main.cpp
    src/app/Screenshot.cpp
    src/app/Screenshot.h
)
set_source_files_properties(qml/Casdap/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(casdap-player
    URI Casdap
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES qml/Casdap/Main.qml
)
target_link_libraries(casdap-player PRIVATE casdapcore Qt6::Gui Qt6::Quick)

install(TARGETS casdap-player RUNTIME DESTINATION bin)

enable_testing()
include(tests/env.cmake)
add_subdirectory(tests)
