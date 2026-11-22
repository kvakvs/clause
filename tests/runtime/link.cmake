include("${HOST_SETTINGS}")
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/source")
file(WRITE "${TEST_DIR}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(RuntimeConsumer LANGUAGES CXX)
set(CLAUSE_BUILD_COMPILER OFF CACHE BOOL "" FORCE)
set(CLAUSE_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${SOURCE_ROOT}" runtime-build)
if(TARGET clause_llvm_sdk)
    message(FATAL_ERROR "Generated-program consumer acquired host LLVM")
endif()
add_executable(linked "${SOURCE_ROOT}/tests/runtime/link_consumer.cpp")
target_link_libraries(linked PRIVATE Clause::generated_program)
set_target_properties(linked PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
# Project headers alone must not satisfy the mandatory runtime dependency.
add_executable(unlinked EXCLUDE_FROM_ALL "${SOURCE_ROOT}/tests/runtime/link_consumer.cpp")
target_link_libraries(unlinked PRIVATE clause_abi)
target_include_directories(unlinked PRIVATE "${SOURCE_ROOT}/runtime/include")
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CXXFLAGS=" "${CMAKE_COMMAND}"
    -S "${TEST_DIR}/source" -B "${TEST_DIR}/build" "-DSOURCE_ROOT=${SOURCE_ROOT}"
    ${host_configure_args}
    RESULT_VARIABLE configured OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT configured STREQUAL "0")
    message(FATAL_ERROR "Consumer configure failed: ${output}${errors}")
endif()
# Every runtime archive and program of the runtime-only build, not only what `linked` needs: codegen_dependency
# configures a runtime-only build but leaves building it to this test (plan 11 step VAL16).
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" --config "${HOST_CONFIG}"
    RESULT_VARIABLE built OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT built STREQUAL "0")
    message(FATAL_ERROR "Matching runtime failed to link: ${output}${errors}")
endif()
execute_process(COMMAND "${TEST_DIR}/build/bin/${HOST_CONFIG}/linked${HOST_SUFFIX}"
    RESULT_VARIABLE ran OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT ran STREQUAL "0" OR NOT output STREQUAL "" OR NOT errors STREQUAL "")
    message(FATAL_ERROR "Linked lifecycle failed or was noisy: ${ran}: ${output}${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" --config "${HOST_CONFIG}" --target unlinked
    RESULT_VARIABLE unlinked OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(unlinked STREQUAL "0" OR NOT "${output}${errors}" MATCHES "Runtime.*start")
    message(FATAL_ERROR "Missing runtime did not fail with missing ABI symbols: ${output}${errors}")
endif()
