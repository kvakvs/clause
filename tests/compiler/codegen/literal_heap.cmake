# Literal heap: compile a module with literals of every kind, link a host consumer that reads them from three runtimes
# (claiming, not claiming, claiming again) and expect its success line.
include("${HOST_SETTINGS}")
set(input "${SOURCE_ROOT}/tests/fixtures/codegen/literal_heap/literals.erl")
set(object_dir "${TEST_DIR}/objects")
file(REMOVE_RECURSE "${object_dir}")
file(MAKE_DIRECTORY "${TEST_DIR}/source")
execute_process(COMMAND "${TOOL}" -${OPTIMIZATION} --emit obj --artifact-dir "${object_dir}" "${input}"
    RESULT_VARIABLE emitted OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT emitted STREQUAL "0" OR NOT output STREQUAL "")
    message(FATAL_ERROR "CLI emission failed: ${emitted}: ${output}${errors}")
endif()
file(GLOB_RECURSE objects "${object_dir}/*.o" "${object_dir}/*.obj")
file(WRITE "${TEST_DIR}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(LiteralHeapConsumer LANGUAGES CXX)
# Link the runtime the parent build already compiled (Clause::generated_program).
include("${RUNTIME_TARGETS}")
add_executable(linked "${SOURCE_ROOT}/tests/compiler/codegen/literal_heap_consumer.cpp" ${OBJECTS})
target_link_libraries(linked PRIVATE Clause::generated_program)
set_target_properties(linked PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${TEST_DIR}/source" -B "${TEST_DIR}/build"
    "-DSOURCE_ROOT=${SOURCE_ROOT}" "-DOBJECTS=${objects}" ${host_configure_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Consumer configure: ${output}${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" --config "${HOST_CONFIG}" --target linked
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Consumer link: ${output}${errors}")
endif()
execute_process(COMMAND "${TEST_DIR}/build/bin/${HOST_CONFIG}/linked${HOST_SUFFIX}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
message("${output}${errors}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Consumer failed: ${result}")
endif()
