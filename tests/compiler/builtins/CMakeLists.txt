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

# Builtins, stubs and auto-imports through a linked program.
if(CLAUSE_BUILD_RUNTIME)
    add_test(NAME builtins_erlang_module COMMAND "${Python3_EXECUTABLE}" "${erlang_module}" program
        "${PROJECT_SOURCE_DIR}" "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/program/$<CONFIG>"
        "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(builtins_erlang_module PROPERTIES TIMEOUT 300
        PASS_REGULAR_EXPRESSION "builtins_erlang_module: ok")
endif()
