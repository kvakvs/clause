# Type inference expectations: every module in tests/fixtures/inference becomes inference_<module>, checking the
# `%% expect:`/`%% today:` signatures above its functions against --print-types.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB fixtures CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/inference/*.erl")
foreach(fixture IN LISTS fixtures)
    get_filename_component(name "${fixture}" NAME_WE)
    add_test(NAME inference_${name} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/expectations.py"
        "$<TARGET_FILE:clau>" "${fixture}")
    set_tests_properties(inference_${name} PROPERTIES TIMEOUT 120)
endforeach()

# Specifications that contradict inference are errors: each fixture of tests/fixtures/inference/contracts lists the
# diagnostics --print-types must report.
add_test(NAME inference_contracts COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/contracts.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/inference/contracts")
set_tests_properties(inference_contracts PROPERTIES TIMEOUT 120)

# Step 58P: a recursive group over its share of the pass budget widens to term() once and is remembered after that;
# 58Q/58S: a map over a tree of two node shapes converges without flipping.
add_test(NAME inference_budgets COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/budgets.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/budgets/$<CONFIG>")
set_tests_properties(inference_budgets PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "inference_budgets: ok")
