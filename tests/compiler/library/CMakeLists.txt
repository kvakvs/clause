# Clause library modules: inventories, stubs and the luerl ledger report (plan step 41J,
# docs/library.md#runtime-module-replacements).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(library_modules "${CMAKE_CURRENT_SOURCE_DIR}/library_modules.py")

# The repository's inventories, module files and modules.terms agree; the inconsistent fixture is refused.
add_test(NAME library_inventory COMMAND "${Python3_EXECUTABLE}" "${library_modules}" check "${PROJECT_SOURCE_DIR}")
set_tests_properties(library_inventory PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "library_inventory: ok")
add_test(NAME library_rejects COMMAND "${Python3_EXECUTABLE}" "${library_modules}" rejects "${PROJECT_SOURCE_DIR}")
set_tests_properties(library_rejects PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "library_rejects: ok")

# The report classifies a real clau run's errors.
add_test(NAME library_report COMMAND "${Python3_EXECUTABLE}" "${library_modules}" reporting "${PROJECT_SOURCE_DIR}"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/report/$<CONFIG>")
set_tests_properties(library_report PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "library_report: ok")

# Inventories match the host OTP; a new one goes through observe, generate, check and clau. Skipped without OTP.
find_program(CLAUSE_TEST_ERL erl HINTS "$ENV{ProgramFiles}/Erlang OTP/bin" DOC "Host Erlang/OTP 29 erl")
set(test_erl "-")
if(CLAUSE_TEST_ERL)
    set(test_erl "${CLAUSE_TEST_ERL}")
endif()
add_test(NAME library_freshness COMMAND "${Python3_EXECUTABLE}" "${library_modules}" freshness "${PROJECT_SOURCE_DIR}"
    "${test_erl}" "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/freshness/$<CONFIG>")
set_tests_properties(library_freshness PROPERTIES TIMEOUT 300 LABELS host_otp
    PASS_REGULAR_EXPRESSION "library_freshness: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")

# Linked programs (plan step 41K): every provided module's stub raises undef in each call form; Clause modules win
# over search paths, explicit inputs win over Clause modules with a warning, missing module names are reserved.
if(CLAUSE_BUILD_RUNTIME)
    foreach(action IN ITEMS stubs precedence types ignored)
        add_test(NAME library_${action} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/library_programs.py" ${action} "${PROJECT_SOURCE_DIR}" "$<TARGET_FILE:clau>"
            "${CMAKE_CURRENT_BINARY_DIR}/${action}/$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(library_${action} PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "library_${action}: ok")
    endforeach()
endif()
