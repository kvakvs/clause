# End-goal program fixtures: offline golden hash checks plus today's compile diagnostics.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME programs_compile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/programs.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/compile/$<CONFIG>")
# Slow (every program fixture compiled at every policy): full gate only (label full_only).
set_tests_properties(programs_compile PROPERTIES TIMEOUT 120 COST 13 LABELS full_only)

# Every program fixture linked through its own manifest under the policy matrix and run against its OTP golden on
# one and several workers (plan step 58), by the executable golden runner.
if(CLAUSE_BUILD_RUNTIME)
    file(GLOB fixtures CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/programs/*/fixture.json")
    foreach(fixture IN LISTS fixtures)
        get_filename_component(fixture_dir "${fixture}" DIRECTORY)
        get_filename_component(name "${fixture_dir}" NAME)
        add_test(NAME programs_${name} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/compiler/executables/run.py" "$<TARGET_FILE:clau>"
            "${CMAKE_CURRENT_BINARY_DIR}/${name}/$<CONFIG>" "${fixture_dir}" "--suffix=${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(programs_${name} PROPERTIES TIMEOUT 600)
    endforeach()
endif()

# Live OTP reproduction of every stdout/exit-status golden; opt-in like other audits.
if(CLAUSE_OTP_AUDITS)
    add_test(NAME programs_oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regenerate.py"
        --escript "${CLAUSE_ESCRIPT}" --check)
    set_tests_properties(programs_oracle PROPERTIES TIMEOUT 600)
endif()
