cmake_minimum_required(VERSION 3.25)

project(
    FrameStepPP
    VERSION 0.1.0
    DESCRIPTION "A C++20 implementation of the FrameStep programming language"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)

option(FRAMESTEPP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FRAMESTEPP_ENABLE_SANITIZERS "Enable supported runtime sanitizers" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

function(framestepp_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    framestepp_set_compiler_warnings(${target})
    framestepp_enable_sanitizers(${target})
endfunction()

add_library(framestepp_core STATIC
    src/diagnostic.cpp
    src/source.cpp
    src/utf8.hpp
)
add_library(FrameStepPP::core ALIAS framestepp_core)

target_include_directories(framestepp_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
framestepp_configure_target(framestepp_core)

add_executable(framestepp app/main.cpp)
target_link_libraries(framestepp PRIVATE FrameStepPP::core)
framestepp_configure_target(framestepp)

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(framestepp_tests tests/source_diagnostic_test.cpp)
    target_link_libraries(framestepp_tests PRIVATE FrameStepPP::core GTest::gtest_main)
    framestepp_configure_target(framestepp_tests)

    include(GoogleTest)
    gtest_discover_tests(framestepp_tests)
endif()
