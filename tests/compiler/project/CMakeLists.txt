# Normal project behavior is covered through real CLI processes; see docs/validation.md (test design).
# Keep only deterministic faults and resource limits unavailable through the CLI.
foreach(suite IN ITEMS limits creation_failure)
    add_executable(project_${suite} ${suite}.cpp)
    clause_project_options(project_${suite})
    target_link_libraries(project_${suite} PRIVATE clause_project clause_project_toml)
    add_test(NAME project_${suite} COMMAND project_${suite} "${CMAKE_CURRENT_BINARY_DIR}/project-${suite}")
endforeach()

add_test(NAME project_cli COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/project-cli"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake")

add_test(NAME project_workflow COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/project/workflow"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/project-workflow"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/workflow.cmake")

set_tests_properties(project_cli project_workflow PROPERTIES TIMEOUT 90)

# The directory index keeps the search precedence of probing (plan step 62E).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME project_directory_index COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/index.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/project-index")
set_tests_properties(project_directory_index PROPERTIES TIMEOUT 120)

# Application resources compile into executables (plan 21 step LIB24).
add_test(NAME project_applications COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/applications.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/project-applications")
set_tests_properties(project_applications PROPERTIES TIMEOUT 600)
