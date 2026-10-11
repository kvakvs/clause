# The erlang library module (plan step 41B, docs/builtins.md#erlang-module).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(erlang_module "${CMAKE_CURRENT_SOURCE_DIR}/erlang_module.py")

# The owned inventory, the generated stub and auto-import table, and the catalog agree; no OTP needed.
add_test(NAME builtins_erlang_inventory COMMAND "${Python3_EXECUTABLE}" "${erlang_module}" inventory
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(builtins_erlang_inventory PROPERTIES TIMEOUT 60
    PASS_REGULAR_EXPRESSION "builtins_erlang_inventory: ok")

# The inventory still matches the host OTP; skipped without one.
find_program(CLAUSE_TEST_ERL erl HINTS "$ENV{ProgramFiles}/Erlang OTP/bin" DOC "Host Erlang/OTP 29 erl")
set(test_erl "-")
if(CLAUSE_TEST_ERL)
    set(test_erl "${CLAUSE_TEST_ERL}")
endif()
add_test(NAME builtins_erlang_freshness COMMAND "${Python3_EXECUTABLE}" "${erlang_module}" freshness
    "${PROJECT_SOURCE_DIR}" "${test_erl}" "${CMAKE_CURRENT_BINARY_DIR}/freshness/$<CONFIG>")
set_tests_properties(builtins_erlang_freshness PROPERTIES TIMEOUT 120 LABELS host_otp
    PASS_REGULAR_EXPRESSION "builtins_erlang_freshness: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")

# Linked programs: builtins, stubs and auto-imports of the erlang module; binary_to_term/1 on mutated input (41E).
if(CLAUSE_BUILD_RUNTIME)
    foreach(fixture IN ITEMS erlang_module etf_fuzz)
        add_test(NAME builtins_${fixture} COMMAND "${Python3_EXECUTABLE}" "${erlang_module}" program
            "${PROJECT_SOURCE_DIR}" "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/${fixture}/$<CONFIG>"
            "${CMAKE_EXECUTABLE_SUFFIX}" ${fixture})
        set_tests_properties(builtins_${fixture} PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "builtins_${fixture}: ok")
    endforeach()
endif()
