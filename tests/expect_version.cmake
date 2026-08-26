foreach(required_variable FRAMESTEPP_EXECUTABLE EXPECTED_VERSION)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${FRAMESTEPP_EXECUTABLE}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_stdout
    ERROR_VARIABLE version_stderr
)

string(REPLACE "\r\n" "\n" version_stdout "${version_stdout}")
string(REPLACE "\r\n" "\n" version_stderr "${version_stderr}")
set(expected_stdout "FrameStep++ ${EXPECTED_VERSION}\n")

if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "expected exit code 0, got '${version_result}'")
endif()
if(NOT version_stderr STREQUAL "")
    message(FATAL_ERROR "expected empty stderr\n${version_stderr}")
endif()
if(NOT version_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR
            "version output did not match\n"
            "--- expected ---\n${expected_stdout}"
            "--- actual ---\n${version_stdout}")
endif()
