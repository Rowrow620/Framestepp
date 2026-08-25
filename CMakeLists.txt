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
include(FetchContent)

set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    utf8proc
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG e5e799221b45bbb90f5fdc5c69b6b8dfbf017e78 # v2.11.3
)
FetchContent_MakeAvailable(utf8proc)

function(framestepp_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    framestepp_set_compiler_warnings(${target})
    framestepp_enable_sanitizers(${target})
endfunction()

add_library(framestepp_core STATIC
    src/ast.cpp
    src/diagnostic.cpp
    src/lexer.cpp
    src/parser.cpp
    src/source.cpp
    src/token.cpp
    src/utf8.hpp
)
add_library(FrameStepPP::core ALIAS framestepp_core)

target_include_directories(framestepp_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(framestepp_core PRIVATE utf8proc)
framestepp_configure_target(framestepp_core)

add_executable(framestepp app/main.cpp)
target_link_libraries(framestepp PRIVATE FrameStepPP::core)
framestepp_configure_target(framestepp)

include(CTest)
if(BUILD_TESTING)
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

    add_executable(framestepp_tests
        tests/lexer_test.cpp
        tests/parser_test.cpp
        tests/source_diagnostic_test.cpp
    )
    target_link_libraries(framestepp_tests PRIVATE FrameStepPP::core GTest::gtest_main)
    framestepp_configure_target(framestepp_tests)

    include(GoogleTest)
    gtest_discover_tests(framestepp_tests)

    add_test(
        NAME CliLexExample
        COMMAND framestepp lex "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.frame"
    )
    set_tests_properties(CliLexExample PROPERTIES PASS_REGULAR_EXPRESSION "end of file")

    add_test(
        NAME CliParseExample
        COMMAND framestepp parse "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.frame"
    )
    set_tests_properties(CliParseExample PROPERTIES PASS_REGULAR_EXPRESSION "Program")
endif()
