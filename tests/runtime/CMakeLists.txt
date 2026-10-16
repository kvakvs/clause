add_executable(runtime_term_tag_tests term_tag.cpp)
clause_project_options(runtime_term_tag_tests)
target_link_libraries(runtime_term_tag_tests PRIVATE clause_runtime)
add_test(NAME runtime_term_tag COMMAND runtime_term_tag_tests)

add_library(runtime_term_layout_tests OBJECT term_layout.cpp)
clause_project_options(runtime_term_layout_tests)
target_link_libraries(runtime_term_layout_tests PRIVATE clause_runtime)
target_include_directories(runtime_term_layout_tests PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src/terms")

add_executable(runtime_feature_tests features.cpp)
clause_project_options(runtime_feature_tests)
target_link_libraries(runtime_feature_tests PRIVATE clause_runtime)
add_test(NAME runtime_features COMMAND runtime_feature_tests)
add_test(NAME runtime_feature_output COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:runtime_feature_tests>" "-DFEATURE_NAME=garbage collection"
    "-DCONTEXT_FIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/diagnostics"
    -P "${PROJECT_SOURCE_DIR}/tests/abi/feature_output.cmake")

foreach(suite IN ITEMS lifecycle_failure memory stack containers integers floats maps bitstrings admission heap_fragments scheduler registration startup)
    add_executable(runtime_${suite} ${suite}.cpp)
    clause_project_options(runtime_${suite})
    target_link_libraries(runtime_${suite} PRIVATE Clause::generated_program)
    add_test(NAME runtime_${suite} COMMAND runtime_${suite})
endforeach()
add_test(NAME runtime_generated_link COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer/$<CONFIG>"
    "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
    "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/link.cmake")
# A separate consumer that rebuilds the whole runtime: full mode only, like the other consumer projects.
set_tests_properties(runtime_generated_link PROPERTIES TIMEOUT 120 LABELS full_only)

# Off-heap binary ownership needs private layout headers; source cannot observe buffer references.
add_executable(runtime_off_heap off_heap.cpp)
clause_project_options(runtime_off_heap)
target_link_libraries(runtime_off_heap PRIVATE Clause::generated_program)
target_include_directories(runtime_off_heap PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_off_heap COMMAND runtime_off_heap)

# Heap walker and verifier invariants over every admitted layout and malformed synthetic areas.
add_executable(runtime_heap_walk heap_walk.cpp)
clause_project_options(runtime_heap_walk)
target_link_libraries(runtime_heap_walk PRIVATE Clause::generated_program)
target_include_directories(runtime_heap_walk PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_heap_walk COMMAND runtime_heap_walk)

# Copying collection: rewritten roots, sharing, binary release, size policy and stale host terms.
add_executable(runtime_collection collection.cpp)
clause_project_options(runtime_collection)
target_link_libraries(runtime_collection PRIVATE Clause::generated_program)
target_include_directories(runtime_collection PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_collection COMMAND runtime_collection)

# Graph copies between heaps: equality, sharing, shared off-heap buffers, survival and failure rollback.
# Native record cells and services over hand-written descriptors; order checks use the private comparator.
add_executable(runtime_records records.cpp)
clause_project_options(runtime_records)
target_link_libraries(runtime_records PRIVATE Clause::generated_program)
target_include_directories(runtime_records PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_records COMMAND runtime_records)

# Fun cells and services over hand-written descriptors: printing, order, call preparation, copy and collection.
add_executable(runtime_funs funs.cpp)
clause_project_options(runtime_funs)
target_link_libraries(runtime_funs PRIVATE Clause::generated_program)
target_include_directories(runtime_funs PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_funs COMMAND runtime_funs)

# Pid and reference identities: admission of issued, forged, foreign and exited pids; references through
# collection, copying and teardown.
add_executable(runtime_identities identities.cpp)
clause_project_options(runtime_identities)
target_link_libraries(runtime_identities PRIVATE Clause::generated_program)
target_include_directories(runtime_identities PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_identities COMMAND runtime_identities)

# Runtime services shared by scheduler workers, stressed from several threads: concurrent atom interning;
# module publication, lookup, calls and external funs, and module pins outliving the runtime.
find_package(Threads REQUIRED)
add_executable(runtime_concurrency concurrency.cpp)
clause_project_options(runtime_concurrency)
target_link_libraries(runtime_concurrency PRIVATE Clause::generated_program Threads::Threads)
add_test(NAME runtime_concurrency COMMAND runtime_concurrency)

# Code server lookups by hash index over many modules and exports: present, missing and wrong-arity names, and
# registrations rejected without indexing anything (plan step 62A).
add_executable(runtime_code_lookup code_lookup.cpp)
clause_project_options(runtime_code_lookup)
target_link_libraries(runtime_code_lookup PRIVATE Clause::generated_program)
add_test(NAME runtime_code_lookup COMMAND runtime_code_lookup)

# The receive-timeout timer wheel: many timers, most cancelled, fire once each, never early, in tick order (62B).
add_executable(runtime_timer_wheel timer_wheel.cpp)
clause_project_options(runtime_timer_wheel)
target_link_libraries(runtime_timer_wheel PRIVATE Clause::generated_program)
target_include_directories(runtime_timer_wheel PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_timer_wheel COMMAND runtime_timer_wheel)

# Port I/O on the reactor: thousands of pipe ports open at once start no thread of their own, and each pipe's
# queued output reaches its reader before the end of input.
add_executable(runtime_port_io port_io.cpp)
clause_project_options(runtime_port_io)
target_link_libraries(runtime_port_io PRIVATE Clause::generated_program)
target_include_directories(runtime_port_io PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_port_io COMMAND runtime_port_io)

# Cooperative executor over hand-written frames: time slices, crash isolation, halts, host invocations and
# releasing processes that never ended.
add_executable(runtime_processes processes.cpp)
clause_project_options(runtime_processes)
target_link_libraries(runtime_processes PRIVATE Clause::generated_program)
target_include_directories(runtime_processes PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_processes COMMAND runtime_processes)

# Messages between contexts: order, self-send, ended receivers, collections, receive positions, refused copies.
add_executable(runtime_messages messages.cpp)
clause_project_options(runtime_messages)
target_link_libraries(runtime_messages PRIVATE Clause::generated_program)
target_include_directories(runtime_messages PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_messages COMMAND runtime_messages)

# Builtins that run in portions as a process's first call, with a collection between every two portions.
add_executable(runtime_portions portions.cpp)
clause_project_options(runtime_portions)
target_link_libraries(runtime_portions PRIVATE Clause::generated_program)
add_test(NAME runtime_portions COMMAND runtime_portions)

add_executable(runtime_copy copy.cpp)
clause_project_options(runtime_copy)
target_link_libraries(runtime_copy PRIVATE Clause::generated_program)
target_include_directories(runtime_copy PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_copy COMMAND runtime_copy)

# Descriptive heap kernel/footprint numbers for docs/runtime-heap.md; printed, not gated.
add_executable(runtime_heap_measurements heap_measurements.cpp)
clause_project_options(runtime_heap_measurements)
target_link_libraries(runtime_heap_measurements PRIVATE Clause::generated_program)
add_test(NAME runtime_heap_measurements COMMAND runtime_heap_measurements)
set_tests_properties(runtime_heap_measurements PROPERTIES TIMEOUT 240 LABELS full_only)

add_executable(runtime_immediate_tests immediate.cpp)
clause_project_options(runtime_immediate_tests)
target_link_libraries(runtime_immediate_tests PRIVATE Clause::generated_program)
add_test(NAME runtime_immediate COMMAND runtime_immediate_tests)

foreach(suite IN ITEMS builtins builtin_bridge)
    add_executable(runtime_${suite} ${suite}.cpp)
    clause_project_options(runtime_${suite})
    target_link_libraries(runtime_${suite} PRIVATE Clause::generated_program)
    add_test(NAME runtime_${suite} COMMAND runtime_${suite})
endforeach()
# Typed builtin adapters: conversion failures, foreign and expired words, throwing bodies.
add_executable(runtime_typed_builtins typed_builtins.cpp)
clause_project_options(runtime_typed_builtins)
target_link_libraries(runtime_typed_builtins PRIVATE Clause::generated_program)
target_include_directories(runtime_typed_builtins PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_typed_builtins COMMAND runtime_typed_builtins)
add_test(NAME runtime_builtin_output COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:runtime_builtin_bridge>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/builtin_output.cmake")

# OTP-generated ~w/erlang:display/1 goldens over every corpus result value, plus bounded-work checks.
add_executable(runtime_printing printing.cpp)
clause_project_options(runtime_printing)
target_link_libraries(runtime_printing PRIVATE Clause::generated_program)
add_test(NAME runtime_printing COMMAND runtime_printing
    "${PROJECT_SOURCE_DIR}/tests/fixtures/printing/values.txt"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/printing/write.txt"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/printing/display.txt")
set_tests_properties(runtime_printing PROPERTIES TIMEOUT 120)

add_executable(runtime_services services.cpp)
clause_project_options(runtime_services)
target_link_libraries(runtime_services PRIVATE Clause::generated_program)
add_test(NAME runtime_services COMMAND runtime_services)
add_test(NAME runtime_service_output COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:runtime_services>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/service_output.cmake")

# The runtime-wide persistent term table: values outlive their writer, replaced values stay readable while held,
# concurrent readers see whole values, nothing stays charged (plan step 41M1).
add_executable(runtime_persistent_terms persistent_terms.cpp)
clause_project_options(runtime_persistent_terms)
target_link_libraries(runtime_persistent_terms PRIVATE Clause::generated_program)
target_include_directories(runtime_persistent_terms PRIVATE "${PROJECT_SOURCE_DIR}/runtime/src")
add_test(NAME runtime_persistent_terms COMMAND runtime_persistent_terms)
set_tests_properties(runtime_persistent_terms PROPERTIES PASS_REGULAR_EXPRESSION "runtime_persistent_terms: ok")

# A runtime test that runs past two minutes has hung (the slowest takes about 20 s under a loaded gate); without its
# own TIMEOUT it would hold the gate for CTest's default 1,500 s.
get_property(runtime_tests DIRECTORY PROPERTY TESTS)
foreach(runtime_test IN LISTS runtime_tests)
    get_test_property(${runtime_test} TIMEOUT runtime_timeout)
    if(NOT runtime_timeout)
        set_tests_properties(${runtime_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()
