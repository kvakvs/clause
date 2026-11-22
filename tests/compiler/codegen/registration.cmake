include("${HOST_SETTINGS}")
if(NOT DEFINED CONSUMER)
    set(CONSUMER "${SOURCE_ROOT}/tests/compiler/codegen/registration_consumer.cpp")
endif()
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/source")
execute_process(COMMAND "${EMITTER}" "${TEST_DIR}/source" ${OPTIMIZATION} RESULT_VARIABLE emitted)
if(NOT emitted STREQUAL "0")
    message(FATAL_ERROR "Module emission failed: ${emitted}")
endif()
file(WRITE "${TEST_DIR}/source/empty.cpp" "int main() { return 0; }\n")
file(WRITE "${TEST_DIR}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(RuntimeConsumer LANGUAGES CXX)
# Link the runtime the parent build already compiled (Clause::generated_program).
include("${RUNTIME_TARGETS}")
add_executable(linked "${CONSUMER}")
target_sources(linked PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/0.obj" "${CMAKE_CURRENT_SOURCE_DIR}/1.obj")
target_link_libraries(linked PRIVATE Clause::generated_program)
set_target_properties(linked PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
# Project headers alone must not satisfy the mandatory runtime dependency.
add_executable(unlinked EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/empty.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/0.obj" "${CMAKE_CURRENT_SOURCE_DIR}/1.obj")
target_link_libraries(unlinked PRIVATE Clause::abi)
target_include_directories(unlinked PRIVATE "${SOURCE_ROOT}/runtime/include")
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CXXFLAGS=" "${CMAKE_COMMAND}"
    -S "${TEST_DIR}/source" -B "${TEST_DIR}/build" "-DSOURCE_ROOT=${SOURCE_ROOT}" "-DCONSUMER=${CONSUMER}"
    ${host_configure_args}
    RESULT_VARIABLE configured OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT configured STREQUAL "0")
    message(FATAL_ERROR "Consumer configure failed: ${output}${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" --config "${HOST_CONFIG}" --target linked
    RESULT_VARIABLE built OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT built STREQUAL "0")
    message(FATAL_ERROR "Matching runtime failed to link: ${output}${errors}")
endif()
execute_process(COMMAND "${TEST_DIR}/build/bin/${HOST_CONFIG}/linked${HOST_SUFFIX}"
    RESULT_VARIABLE ran OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT ran STREQUAL "0" OR NOT output STREQUAL "" OR NOT errors STREQUAL "")
    message(FATAL_ERROR "Linked lifecycle failed or was noisy: ${ran}: ${output}${errors}")
endif()
# The generated objects need the runtime, not only its headers; one test checks it (CHECK_UNLINKED, plan 11 VAL14).
if(CHECK_UNLINKED)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" --config "${HOST_CONFIG}" --target unlinked
        RESULT_VARIABLE unlinked OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(unlinked STREQUAL "0" OR NOT "${output}${errors}" MATCHES "CLAUSE_register_module_v4")
        message(FATAL_ERROR "Missing runtime did not fail with missing ABI symbols: ${output}${errors}")
    endif()
endif()
