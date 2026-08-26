cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FRAMESTEPP_EXECUTABLE)
    message(FATAL_ERROR "FRAMESTEPP_EXECUTABLE is required")
endif()
if(NOT EXISTS "${FRAMESTEPP_EXECUTABLE}")
    message(FATAL_ERROR "FrameStep++ executable does not exist: ${FRAMESTEPP_EXECUTABLE}")
endif()

get_filename_component(tool_directory "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(project_root "${tool_directory}/.." ABSOLUTE)
set(corpus_directory "${project_root}/tests/conformance")

function(normalize_line_endings input_text output_name)
    string(REPLACE "\r\n" "\n" normalized "${input_text}")
    set(${output_name} "${normalized}" PARENT_SCOPE)
endfunction()

function(read_expected path output_name)
    if(path AND EXISTS "${path}")
        file(READ "${path}" contents)
    else()
        set(contents "")
    endif()
    normalize_line_endings("${contents}" contents)
    set(${output_name} "${contents}" PARENT_SCOPE)
endfunction()

function(run_case engine_name executable source_file expected_exit expected_stdout_file
         expected_stderr_file)
    execute_process(
        COMMAND "${executable}" run "${source_file}"
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
        TIMEOUT 15
    )

    normalize_line_endings("${actual_stdout}" actual_stdout)
    normalize_line_endings("${actual_stderr}" actual_stderr)
    file(TO_NATIVE_PATH "${source_file}" native_source_file)
    string(REPLACE "${source_file}" "SOURCE_FILE_PLACEHOLDER" actual_stderr
                   "${actual_stderr}")
    string(REPLACE "${native_source_file}" "SOURCE_FILE_PLACEHOLDER" actual_stderr
                   "${actual_stderr}")

    read_expected("${expected_stdout_file}" expected_stdout)
    read_expected("${expected_stderr_file}" expected_stderr)

    get_filename_component(case_name "${source_file}" NAME_WE)
    if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR
                "${engine_name} `${case_name}` exit code mismatch: "
                "expected ${expected_exit}, got ${actual_exit}")
    endif()
    if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
        message(FATAL_ERROR
                "${engine_name} `${case_name}` stdout mismatch\n"
                "--- expected ---\n${expected_stdout}"
                "--- actual ---\n${actual_stdout}")
    endif()
    if(NOT "${actual_stderr}" STREQUAL "${expected_stderr}")
        message(FATAL_ERROR
                "${engine_name} `${case_name}` stderr mismatch\n"
                "--- expected ---\n${expected_stderr}"
                "--- actual ---\n${actual_stderr}")
    endif()
endfunction()

file(GLOB accepted_sources "${corpus_directory}/accepted/*.frame")
file(GLOB rejected_sources "${corpus_directory}/rejected/*.frame")
list(SORT accepted_sources)
list(SORT rejected_sources)

if(NOT accepted_sources OR NOT rejected_sources)
    message(FATAL_ERROR "the conformance corpus must contain accepted and rejected cases")
endif()

set(reference_enabled FALSE)
if(DEFINED FRAMESTEP_REFERENCE_EXECUTABLE AND NOT FRAMESTEP_REFERENCE_EXECUTABLE STREQUAL "")
    set(reference_enabled TRUE)
    if(NOT EXISTS "${FRAMESTEP_REFERENCE_EXECUTABLE}")
        message(FATAL_ERROR
                "FrameStep reference executable does not exist: "
                "${FRAMESTEP_REFERENCE_EXECUTABLE}")
    endif()

    file(REAL_PATH "${FRAMESTEPP_EXECUTABLE}" framestepp_real_path)
    file(REAL_PATH "${FRAMESTEP_REFERENCE_EXECUTABLE}" framestep_real_path)
    if(framestepp_real_path STREQUAL framestep_real_path)
        message(FATAL_ERROR
                "FrameStep++ and FrameStep reference executables must be different files")
    endif()

    execute_process(
        COMMAND "${FRAMESTEP_REFERENCE_EXECUTABLE}" --version
        RESULT_VARIABLE reference_version_exit
        OUTPUT_VARIABLE reference_version_stdout
        ERROR_VARIABLE reference_version_stderr
        TIMEOUT 5
    )
    normalize_line_endings("${reference_version_stdout}" reference_version_stdout)
    normalize_line_endings("${reference_version_stderr}" reference_version_stderr)
    if(NOT "${reference_version_exit}" STREQUAL "0" OR
       NOT "${reference_version_stdout}" STREQUAL "framestep 1.0.0\n" OR
       NOT "${reference_version_stderr}" STREQUAL "")
        message(FATAL_ERROR
                "FRAMESTEP_REFERENCE_EXECUTABLE must identify itself as framestep 1.0.0")
    endif()
endif()

if(reference_enabled)
    set(report_header "| Case | Expected boundary | FrameStep++ | FrameStep |\n")
    set(report_separator "| --- | --- | --- | --- |\n")
else()
    set(report_header "| Case | Expected boundary | FrameStep++ |\n")
    set(report_separator "| --- | --- | --- |\n")
endif()
set(report_rows "")
set(case_count 0)

foreach(source_file IN LISTS accepted_sources rejected_sources)
    get_filename_component(case_directory "${source_file}" DIRECTORY)
    get_filename_component(case_name "${source_file}" NAME_WE)
    get_filename_component(case_group "${case_directory}" NAME)

    if(case_group STREQUAL "accepted")
        set(expected_exit 0)
        set(expected_boundary "accepted program")
        set(expected_stdout_file "${case_directory}/${case_name}.stdout")
        set(expected_stderr_file "")
        if(NOT EXISTS "${expected_stdout_file}")
            message(FATAL_ERROR "accepted case `${case_name}` is missing its .stdout sidecar")
        endif()
    else()
        set(expected_exit 1)
        set(expected_boundary "rejected program")
        set(expected_stdout_file "${case_directory}/${case_name}.stdout")
        set(expected_stderr_file "${case_directory}/${case_name}.stderr")
        if(NOT EXISTS "${expected_stderr_file}")
            message(FATAL_ERROR "rejected case `${case_name}` is missing its .stderr sidecar")
        endif()
    endif()

    run_case("FrameStep++" "${FRAMESTEPP_EXECUTABLE}" "${source_file}" "${expected_exit}"
             "${expected_stdout_file}" "${expected_stderr_file}")
    if(reference_enabled)
        run_case("FrameStep" "${FRAMESTEP_REFERENCE_EXECUTABLE}" "${source_file}"
                 "${expected_exit}" "${expected_stdout_file}" "${expected_stderr_file}")
        string(APPEND report_rows
               "| `${case_name}` | ${expected_boundary} | passed | passed |\n")
    else()
        string(APPEND report_rows "| `${case_name}` | ${expected_boundary} | passed |\n")
    endif()
    math(EXPR case_count "${case_count} + 1")
endforeach()

string(CONCAT report
       "# Conformance report\n\n"
       "Generated from the source fixtures in `tests/conformance`. The corpus checks\n"
       "accepted programs, rejected programs, ordered output, diagnostics, and source\n"
       "locations.\n\n"
       "${report_header}${report_separator}${report_rows}\n"
       "FrameStep++ passed ${case_count}/${case_count} cases.\n")
if(reference_enabled)
    string(APPEND report "FrameStep matched ${case_count}/${case_count} shared cases.\n")
else()
    string(APPEND report
           "The optional FrameStep comparison is not part of the self-contained C++ test run.\n")
endif()

if(DEFINED EXPECTED_REPORT_FILE)
    read_expected("${EXPECTED_REPORT_FILE}" expected_report)
    if(NOT "${report}" STREQUAL "${expected_report}")
        message(FATAL_ERROR
                "the checked-in conformance report is stale; regenerate it with REPORT_FILE")
    endif()
endif()

if(DEFINED REPORT_FILE)
    get_filename_component(report_directory "${REPORT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${report_directory}")
    file(WRITE "${REPORT_FILE}" "${report}")
endif()

if(reference_enabled)
    message(STATUS
            "FrameStep++ and FrameStep passed ${case_count}/${case_count} conformance cases")
else()
    message(STATUS "FrameStep++ passed ${case_count}/${case_count} conformance cases")
endif()
