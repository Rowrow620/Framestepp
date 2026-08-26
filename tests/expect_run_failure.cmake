foreach(required_variable FRAMESTEPP_EXECUTABLE SOURCE_FILE EXPECTED_STDERR_FILE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(DEFINED INPUT_FILE)
    execute_process(
        COMMAND "${FRAMESTEPP_EXECUTABLE}" run "${SOURCE_FILE}"
        INPUT_FILE "${INPUT_FILE}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
else()
    execute_process(
        COMMAND "${FRAMESTEPP_EXECUTABLE}" run "${SOURCE_FILE}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
endif()

function(normalize_captured_crlf variable_name)
    string(REPLACE "\r\n" "\n" normalized "${${variable_name}}")
    set(${variable_name} "${normalized}" PARENT_SCOPE)
endfunction()

normalize_captured_crlf(run_stdout)
normalize_captured_crlf(run_stderr)

if(NOT run_result EQUAL 1)
    message(FATAL_ERROR "expected exit code 1, got '${run_result}'\n${run_stdout}${run_stderr}")
endif()

set(expected_stdout "")
if(DEFINED EXPECTED_STDOUT_FILE)
    file(READ "${EXPECTED_STDOUT_FILE}" expected_stdout)
    string(REPLACE "\r\n" "\n" expected_stdout "${expected_stdout}")
endif()
if(NOT run_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR
            "stdout did not match\n"
            "--- expected ---\n${expected_stdout}"
            "--- actual ---\n${run_stdout}")
endif()

file(READ "${EXPECTED_STDERR_FILE}" expected_stderr)
string(REPLACE "SOURCE_FILE_PLACEHOLDER" "${SOURCE_FILE}" expected_stderr "${expected_stderr}")
normalize_captured_crlf(expected_stderr)
if(NOT run_stderr STREQUAL expected_stderr)
    message(FATAL_ERROR
            "stderr did not match\n"
            "--- expected ---\n${expected_stderr}"
            "--- actual ---\n${run_stderr}")
endif()
