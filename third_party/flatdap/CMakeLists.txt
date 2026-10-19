add_library(flatdap_link STATIC
    link/frame.cpp
    link/frame.h
    link/proto.h
    link/session.cpp
    link/session.h
)
set_target_properties(flatdap_link PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(flatdap_link PUBLIC cxx_std_20)
target_include_directories(flatdap_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
