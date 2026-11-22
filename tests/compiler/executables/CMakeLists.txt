# Executable golden runner (tests/fixtures/executables/README.md): every case directory holding a golden.json
# links under the fast/full policy matrix and runs against its OTP-generated stdout and exit status.
if(CLAUSE_BUILD_RUNTIME)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # Area labels, smoke set and full_only cases of the golden cases (plan 11 step VAL7): a case of more than about
    # 10 s alone runs in the full gate only (label full_only) unless it is in the smoke set; ctest -L <area> runs one
    # area, the library work's quality gate.
    include("${CMAKE_CURRENT_SOURCE_DIR}/areas.cmake")
    file(GLOB goldens CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/executables/*/golden.json")
    set(golden_cases)
    # The host OTP of host_otp cases, optional: without it they skip.
    find_program(CLAUSE_ESCRIPT escript DOC "Host Erlang/OTP 29 or newer escript for optional live audits")
    foreach(golden IN LISTS goldens)
        get_filename_component(case_dir "${golden}" DIRECTORY)
        get_filename_component(case "${case_dir}" NAME)
        list(APPEND golden_cases ${case})
        # A case of OTP nodes or OTP's epmd ("host_otp": true) needs the host OTP; without it the runner skips it
        # (plan 30 step DIST3a).
        file(READ "${golden}" golden_text)
        string(JSON host_otp ERROR_VARIABLE no_host_otp GET "${golden_text}" host_otp)
        set(otp_arguments)
        if(host_otp AND CLAUSE_ESCRIPT)
            set(otp_arguments "--escript=${CLAUSE_ESCRIPT}")
        endif()
        add_test(NAME executables_${case} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
            "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/${case}/$<CONFIG>" "${case_dir}"
            "--suffix=${CMAKE_EXECUTABLE_SUFFIX}" "--epmd-executable=$<TARGET_FILE:clause_epmd>" ${otp_arguments})
        set_tests_properties(executables_${case} PROPERTIES TIMEOUT 600)
        # One label list per case, set once: the area, host_otp, full_only.
        golden_case_area(${case} labels)
        if(host_otp)
            list(APPEND labels host_otp)
            set_tests_properties(executables_${case} PROPERTIES SKIP_RETURN_CODE 77)
        endif()
        golden_case_full_only(${case} full_only)
        if(full_only)
            list(APPEND labels full_only)
            set_tests_properties(executables_${case} PROPERTIES COST 15)
        endif()
        set_tests_properties(executables_${case} PROPERTIES LABELS "${labels}")
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
    golden_check_names(${golden_cases})
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
