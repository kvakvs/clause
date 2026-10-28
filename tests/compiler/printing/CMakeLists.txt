# Term printing: compiled erlang:display/1 output against OTP goldens (runtime goldens: tests/runtime/printing.cpp).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CLAUSE_BUILD_RUNTIME)
    add_test(NAME printing_display COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/display.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${CMAKE_CURRENT_BINARY_DIR}/display/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(printing_display PROPERTIES TIMEOUT 600)
endif()

# Erlang source printing: every parseable fixture module prints, parses back to the same tree and prints the same.
add_test(NAME printing_source COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/source_roundtrip.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/source")
# Slow (about 47 s alone, every fixture module printed twice): full gate only (label full_only).
set_tests_properties(printing_source PROPERTIES TIMEOUT 300 COST 10 LABELS full_only)

# Live OTP reproduction of the printing goldens; opt-in like other audits.
if(CLAUSE_OTP_AUDITS)
    add_test(NAME printing_oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regenerate.py"
        --escript "${CLAUSE_ESCRIPT}" --check)
    set_tests_properties(printing_oracle PROPERTIES TIMEOUT 600)
endif()
