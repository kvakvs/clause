# Added for the SQLite cache backend (plan step 62D5): the store under threads and processes, and clau runs sharing
# one build directory.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(cache_store store.cpp)
clause_project_options(cache_store)
target_include_directories(cache_store PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(cache_store PRIVATE clause_cache)
add_test(NAME cache_store COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/store.py"
    "$<TARGET_FILE:cache_store>" "${CMAKE_CURRENT_BINARY_DIR}/store/$<CONFIG>")
set_tests_properties(cache_store PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "store: ok")

add_test(NAME cache_projects COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/projects.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/projects/$<CONFIG>")
set_tests_properties(cache_projects PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "projects: ok")

# Inference cache (plan step 58W): covered modules reuse their inference results. The work directory stays outside
# tests/compiler, which the implicit pattern */compiler would cover.
add_test(NAME cache_inference COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/inference.py"
    "$<TARGET_FILE:clau>" "${PROJECT_BINARY_DIR}/tests/inference-cache/$<CONFIG>")
set_tests_properties(cache_inference PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "inference: ok")
