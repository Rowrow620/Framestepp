foreach(required_variable FRAMESTEPP_EXECUTABLE SOURCE_FILE EXPECTED_FILE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(normalize_captured_crlf variable_name)
    string(REPLACE "\r\n" "\n" normalized "${${variable_name}}")
    set(${variable_name} "${normalized}" PARENT_SCOPE)
endfunction()

function(run_program output_name)
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

    normalize_captured_crlf(run_stdout)
    normalize_captured_crlf(run_stderr)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "expected exit code 0, got '${run_result}'\n${run_stderr}")
    endif()
    if(NOT run_stderr STREQUAL "")
        message(FATAL_ERROR "expected empty stderr\n${run_stderr}")
    endif()
    set(${output_name} "${run_stdout}" PARENT_SCOPE)
endfunction()

file(READ "${EXPECTED_FILE}" expected_output)
normalize_captured_crlf(expected_output)

run_program(first_output)
run_program(second_output)

if(NOT first_output STREQUAL second_output)
    message(FATAL_ERROR "two runs produced different output")
endif()
if(NOT first_output STREQUAL expected_output)
    message(FATAL_ERROR
            "program output did not match the fixture\n"
            "--- expected ---\n${expected_output}"
            "--- actual ---\n${first_output}")
endif()
