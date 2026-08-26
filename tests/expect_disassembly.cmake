foreach(required_variable IN ITEMS FRAMESTEPP_EXECUTABLE SOURCE_FILE EXPECTED_FILE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "missing required variable: ${required_variable}")
    endif()
endforeach()

function(run_disassembly output_name)
    execute_process(
        COMMAND "${FRAMESTEPP_EXECUTABLE}" disassemble "${SOURCE_FILE}"
        RESULT_VARIABLE disassemble_result
        OUTPUT_VARIABLE disassemble_stdout
        ERROR_VARIABLE disassemble_stderr
        ENCODING UTF-8
    )

    if(NOT disassemble_result STREQUAL "0")
        message(FATAL_ERROR
                "expected exit code 0, got '${disassemble_result}'\n${disassemble_stderr}")
    endif()
    if(NOT disassemble_stderr STREQUAL "")
        message(FATAL_ERROR "expected empty stderr\n${disassemble_stderr}")
    endif()

    string(REPLACE "\r\n" "\n" disassemble_stdout "${disassemble_stdout}")
    string(REPLACE "\r" "\n" disassemble_stdout "${disassemble_stdout}")
    set(${output_name} "${disassemble_stdout}" PARENT_SCOPE)
endfunction()

run_disassembly(first_output)
run_disassembly(second_output)

if(NOT first_output STREQUAL second_output)
    message(FATAL_ERROR "disassembly changed between identical runs")
endif()

file(READ "${EXPECTED_FILE}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r" "\n" expected_output "${expected_output}")

if(NOT first_output STREQUAL expected_output)
    message(FATAL_ERROR
            "disassembly did not match ${EXPECTED_FILE}\n"
            "--- expected ---\n${expected_output}"
            "--- actual ---\n${first_output}")
endif()
