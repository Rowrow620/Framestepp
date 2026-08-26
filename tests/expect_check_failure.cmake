foreach(required_variable IN ITEMS FRAMESTEPP_EXECUTABLE SOURCE_FILE EXPECTED_MESSAGE
                                   EXPECTED_LOCATION EXPECTED_CARET)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "missing required variable: ${required_variable}")
    endif()
endforeach()

execute_process(
    COMMAND "${FRAMESTEPP_EXECUTABLE}" check "${SOURCE_FILE}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_stdout
    ERROR_VARIABLE check_stderr
    ENCODING UTF-8
)

set(check_output "${check_stdout}${check_stderr}")
if(NOT check_result STREQUAL "1")
    message(FATAL_ERROR "expected exit code 1, got '${check_result}'\n${check_output}")
endif()

foreach(expected_variable IN ITEMS EXPECTED_MESSAGE EXPECTED_LOCATION EXPECTED_CARET)
    string(FIND "${check_output}" "${${expected_variable}}" match_position)
    if(match_position EQUAL -1)
        message(FATAL_ERROR
                "output did not contain ${expected_variable}='${${expected_variable}}'\n${check_output}")
    endif()
endforeach()
