# Executable golden runner (tests/fixtures/executables/README.md): every case directory holding a golden.json
# links under the fast/full policy matrix and runs against its OTP-generated stdout and exit status.
if(CLAUSE_BUILD_RUNTIME)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # Cases too slow for the fast gate (qlc_queries: about 77 s alone, its query module through every policy): full
    # gate only (label full_only).
    set(full_only_cases qlc_queries)
    file(GLOB goldens CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/executables/*/golden.json")
    foreach(golden IN LISTS goldens)
        get_filename_component(case_dir "${golden}" DIRECTORY)
        get_filename_component(case "${case_dir}" NAME)
        add_test(NAME executables_${case} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
            "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/${case}/$<CONFIG>" "${case_dir}"
            "--suffix=${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(executables_${case} PROPERTIES TIMEOUT 600)
        if(case IN_LIST full_only_cases)
            set_tests_properties(executables_${case} PROPERTIES COST 15 LABELS full_only)
        endif()
        # A case whose sources name a parse transform compiles through the transform host: it waits for the host's
        # setup instead of its lock (added for the transform host setup).
        file(GLOB_RECURSE case_sources "${case_dir}/*.erl" "${case_dir}/*.hrl")
        foreach(source IN LISTS case_sources)
            file(STRINGS "${source}" transform_lines REGEX "parse_transform|ms_transform\\.hrl|qlc\\.hrl|merl\\.hrl")
            if(transform_lines)
                set_tests_properties(executables_${case} PROPERTIES FIXTURES_REQUIRED transform_host)
                break()
            endif()
        endforeach()
    endforeach()
    # A deliberately wrong golden must fail with a readable diff; a stale one before linking.
    add_test(NAME executables_selfcheck COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/selfcheck.py"
        "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/$<CONFIG>"
        "--suffix=${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(executables_selfcheck PROPERTIES TIMEOUT 600)
endif()

# Live OTP reproduction of every case golden; opt-in like other audits.
if(CLAUSE_OTP_AUDITS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME executables_oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regenerate.py"
        --escript "${CLAUSE_ESCRIPT}" --check)
    set_tests_properties(executables_oracle PROPERTIES TIMEOUT 600)
endif()
