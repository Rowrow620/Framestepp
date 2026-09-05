cmake_minimum_required(VERSION 3.25)

project(
    FrameStepPP
    VERSION 1.0.0
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
    src/bytecode.cpp
    src/compiler.cpp
    src/diagnostic.cpp
    src/lexer.cpp
    src/parser.cpp
    src/source.cpp
    src/token.cpp
    src/type.cpp
    src/type_checker.cpp
    src/utf8.hpp
    src/value.cpp
    src/verifier.cpp
    src/vm.cpp
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
target_compile_definitions(framestepp PRIVATE FRAMESTEPP_VERSION="${PROJECT_VERSION}")
framestepp_configure_target(framestepp)

add_custom_target(
    conformance-report
    COMMAND "${CMAKE_COMMAND}"
            "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
            "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/conformance.md"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_conformance.cmake"
    DEPENDS framestepp
    COMMENT "Running the source-level conformance corpus"
    VERBATIM
)

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
        tests/bytecode_test.cpp
        tests/compiler_test.cpp
        tests/lexer_test.cpp
        tests/parser_test.cpp
        tests/source_diagnostic_test.cpp
        tests/type_checker_test.cpp
        tests/value_test.cpp
        tests/verifier_test.cpp
        tests/vm_test.cpp
    )
    target_link_libraries(framestepp_tests PRIVATE FrameStepPP::core GTest::gtest_main)
    framestepp_configure_target(framestepp_tests)

    include(GoogleTest)
    gtest_discover_tests(framestepp_tests)

    add_test(
        NAME CliLexExample
        COMMAND framestepp lex "${CMAKE_CURRENT_SOURCE_DIR}/examples/connection.frame"
    )
    set_tests_properties(CliLexExample PROPERTIES PASS_REGULAR_EXPRESSION "end of file")

    add_test(
        NAME CliParseExample
        COMMAND framestepp parse "${CMAKE_CURRENT_SOURCE_DIR}/examples/connection.frame"
    )
    set_tests_properties(CliParseExample PROPERTIES PASS_REGULAR_EXPRESSION "Program")

    add_test(
        NAME CliCheckValidFixture
        COMMAND framestepp check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_check/valid.frame"
    )
    set_tests_properties(CliCheckValidFixture PROPERTIES PASS_REGULAR_EXPRESSION "type check passed")

    add_test(NAME CliHelpIncludesDisassemble COMMAND framestepp --help)
    set_tests_properties(CliHelpIncludesDisassemble PROPERTIES
                         PASS_REGULAR_EXPRESSION "framestepp disassemble <FILE>")

    add_test(NAME CliHelpIncludesRun COMMAND framestepp --help)
    set_tests_properties(CliHelpIncludesRun PROPERTIES
                         PASS_REGULAR_EXPRESSION "framestepp run <FILE>")

    add_test(
        NAME CliVersion
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DEXPECTED_VERSION=${PROJECT_VERSION}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_version.cmake"
    )

    add_test(
        NAME CliDisassembleFixture
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/bytecode/program.frame"
                "-DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/bytecode/program.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_disassembly.cmake"
    )

    add_test(
        NAME CliRejectsTypeMismatch
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_check/type_mismatch.frame"
                "-DEXPECTED_MESSAGE=variable `score` expects Int, found String"
                "-DEXPECTED_LOCATION=:1:18"
                "-DEXPECTED_CARET=^^^^^^^^^^^"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_check_failure.cmake"
    )

    add_test(
        NAME CliRejectsUnsafeGlobalInitializer
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_check/unsafe_global_initializer.frame"
                "-DEXPECTED_MESSAGE=global initializers cannot call user-defined functions"
                "-DEXPECTED_LOCATION=:1:19"
                "-DEXPECTED_CARET=^^^^"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_check_failure.cmake"
    )

    add_test(
        NAME CliDisassembleRejectsTypeMismatch
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DCOMMAND_NAME=disassemble"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_check/type_mismatch.frame"
                "-DEXPECTED_MESSAGE=variable `score` expects Int, found String"
                "-DEXPECTED_LOCATION=:1:18"
                "-DEXPECTED_CARET=^^^^^^^^^^^"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_check_failure.cmake"
    )

    add_test(
        NAME CliRunConnection
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/connection.frame"
                "-DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/connection.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run.cmake"
    )

    add_test(
        NAME CliRunFibonacci
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/fibonacci.frame"
                "-DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/fibonacci.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run.cmake"
    )

    add_test(
        NAME CliRunBossFight
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/boss_fight.frame"
                "-DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/boss_fight.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run.cmake"
    )

    add_test(
        NAME CliRunInput
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/input.frame"
                "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/name.input"
                "-DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/input.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run.cmake"
    )

    add_test(
        NAME CliRunPreservesOutputBeforeRuntimeFailure
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/divide_by_zero.frame"
                "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/before.expected"
                "-DEXPECTED_STDERR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/divide_by_zero.stderr.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run_failure.cmake"
    )

    add_test(
        NAME CliRunRejectsStaticErrorBeforeExecution
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/static_error.frame"
                "-DEXPECTED_STDERR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/static_error.stderr.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run_failure.cmake"
    )

    add_test(
        NAME CliRunRejectsInputEof
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/input_eof.frame"
                "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/empty.input"
                "-DEXPECTED_STDERR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/input_eof.stderr.expected"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_run_failure.cmake"
    )

    add_test(
        NAME ConformanceCorpus
        COMMAND "${CMAKE_COMMAND}"
                "-DFRAMESTEPP_EXECUTABLE=$<TARGET_FILE:framestepp>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_conformance.cmake"
    )
    set_tests_properties(ConformanceCorpus PROPERTIES LABELS conformance)
endif()
