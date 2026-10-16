# Added for parse transforms: term codec invariants against OTP-generated samples (docs/transforms.md).
add_executable(transforms_terms terms.cpp)
clause_project_options(transforms_terms)
target_include_directories(transforms_terms PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(transforms_terms PRIVATE clause_transforms)
add_test(NAME transforms_terms
    COMMAND transforms_terms "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms/terms")
set_tests_properties(transforms_terms PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "transforms_terms: ok")

# Added for parse transforms: --print-abstr forms against OTP epp goldens, compared as terms.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(transforms_compare compare.cpp)
clause_project_options(transforms_compare)
target_include_directories(transforms_compare PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(transforms_compare PRIVATE clause_transforms)
add_test(NAME transforms_abstract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/abstract.py"
    "$<TARGET_FILE:clau>" "$<TARGET_FILE:transforms_compare>" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms/abstract"
    "${CMAKE_CURRENT_BINARY_DIR}/abstract/$<CONFIG>")
set_tests_properties(transforms_abstract PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "transforms_abstract: ok")

# Added for parse transforms: .abstr inputs linked into programs, in a project, and malformed forms.
add_test(NAME transforms_inputs COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/inputs.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms/inputs" "${CMAKE_CURRENT_BINARY_DIR}/inputs/$<CONFIG>")
set_tests_properties(transforms_inputs PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "transforms_inputs: ok")

# Added for parse transforms: the loader on the host Erlang/OTP. Tests labeled host_otp skip without one.
find_program(CLAUSE_TEST_ERL erl HINTS "$ENV{ProgramFiles}/Erlang OTP/bin" DOC "Host Erlang/OTP 29 erl for parse transform tests")
if(CLAUSE_TEST_ERL)
    set(test_erl "${CLAUSE_TEST_ERL}")
else()
    set(test_erl "-")
endif()
add_executable(transforms_loader loader.cpp)
clause_project_options(transforms_loader)
target_include_directories(transforms_loader PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(transforms_loader PRIVATE clause_transforms)
add_test(NAME transforms_loader
    COMMAND transforms_loader "${test_erl}" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms/loader")
set_tests_properties(transforms_loader PROPERTIES TIMEOUT 300 LABELS host_otp
    PASS_REGULAR_EXPRESSION "transforms_loader: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")

# Added for parse transforms: transforms in the compile pipeline (host OTP), and the error without a host OTP.
add_test(NAME transforms_pipeline COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/pipeline.py" otp
    "$<TARGET_FILE:clau>" "${test_erl}" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms"
    "${CMAKE_CURRENT_BINARY_DIR}/pipeline/$<CONFIG>")
set_tests_properties(transforms_pipeline PROPERTIES TIMEOUT 600 LABELS host_otp
    PASS_REGULAR_EXPRESSION "transforms_pipeline: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")
add_test(NAME transforms_no_otp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/pipeline.py" none
    "$<TARGET_FILE:clau>" "-" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms"
    "${CMAKE_CURRENT_BINARY_DIR}/no-otp/$<CONFIG>")
set_tests_properties(transforms_no_otp PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "transforms_no_otp: ok")

# Added for parse transforms: transform modules from a project: sources, helpers, precompiled dependencies.
add_test(NAME transforms_project COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/project.py"
    "$<TARGET_FILE:clau>" "${test_erl}" "${PROJECT_SOURCE_DIR}/tests/fixtures/transforms"
    "${CMAKE_CURRENT_BINARY_DIR}/project/$<CONFIG>")
set_tests_properties(transforms_project PROPERTIES TIMEOUT 600 LABELS host_otp
    PASS_REGULAR_EXPRESSION "transforms_project: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")

# Added for the build directory (62D1): loader workspaces below <build_dir>/tmp, nothing left behind.
add_test(NAME transforms_build_dir COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_dir.py"
    "$<TARGET_FILE:clau>" "${test_erl}" "${CMAKE_CURRENT_BINARY_DIR}/build_dir/$<CONFIG>")
set_tests_properties(transforms_build_dir PROPERTIES TIMEOUT 600 LABELS host_otp
    PASS_REGULAR_EXPRESSION "build_dir: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")

# Added for build caches (62D2): transform results reused, rebuilt after each kind of edit, damaged entries rebuilt.
add_test(NAME transforms_cache COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cache.py"
    "$<TARGET_FILE:clau>" "${test_erl}" "${CMAKE_CURRENT_BINARY_DIR}/cache/$<CONFIG>")
set_tests_properties(transforms_cache PROPERTIES TIMEOUT 600 COST 12 LABELS host_otp
    PASS_REGULAR_EXPRESSION "cache: ok" SKIP_REGULAR_EXPRESSION "SKIP: no host Erlang/OTP")
