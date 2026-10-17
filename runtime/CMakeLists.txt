include(${PROJECT_SOURCE_DIR}/cmake/BoostDependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Pcre2Dependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/ZlibDependencies.cmake)

add_library(clause_runtime STATIC src/runtime.cpp src/diagnostics/features.cpp
    src/terms/bit_factory.cpp src/terms/bit_access.cpp src/terms/word_order.cpp src/terms/bit_float.cpp src/terms/bit_numeric.cpp src/terms/bit_utf.cpp src/terms/bit_services.cpp
    src/terms/map_factory.cpp src/terms/map_access.cpp src/terms/map_services.cpp src/terms/map_tree.cpp src/terms/map_cursor.cpp
    src/terms/records.cpp src/terms/record_services.cpp src/terms/funs.cpp src/terms/fun_services.cpp src/terms/identities.cpp src/terms/dynamic_calls.cpp
    src/terms/float_factory.cpp src/terms/float_literal.cpp src/terms/float_operations.cpp src/terms/numeric_conversions.cpp src/terms/numeric_order.cpp src/terms/numeric_service.cpp
    src/terms/integer_values.cpp src/terms/integer_factory.cpp src/terms/integer_operations.cpp src/terms/integer_access.cpp src/terms/integer_literal.cpp
    src/terms/integer_service.cpp src/terms/integer_decimal.cpp src/terms/integer_sum.cpp src/terms/integer_words.cpp
    src/memory/heap_terms.cpp src/memory/heap_publication.cpp
    src/terms/container_access.cpp src/terms/container_factory.cpp src/terms/container_services.cpp src/terms/structural_order.cpp
    src/process/generated_calls.cpp src/process/exceptions.cpp src/process/exits.cpp src/process/receive.cpp src/process/stack.cpp src/process/profile.cpp src/process/call_trace.cpp src/process/context.cpp src/process/identities.cpp src/process/ownership.cpp src/process/storage.cpp
    src/memory/heap.cpp src/memory/heap_storage.cpp src/memory/heap_blocks.cpp src/memory/heap_reservation.cpp src/memory/off_heap.cpp src/memory/heap_collect.cpp src/memory/heap_walk.cpp src/memory/heap_verify.cpp src/memory/copy.cpp src/memory/persistent_terms.cpp
    src/scheduler/services.cpp src/scheduler/registry.cpp src/scheduler/transitions.cpp src/scheduler/executor.cpp src/scheduler/timer_wheel.cpp src/scheduler/timers.cpp src/scheduler/introspection.cpp src/scheduler/aliases.cpp src/scheduler/signals.cpp src/scheduler/ports.cpp src/ports/fd.cpp src/ports/input.cpp src/ports/io.cpp src/ports/spawn.cpp src/ports/file.cpp src/ports/file_info.cpp src/ports/file_paths.cpp src/ports/value.cpp src/ports/sockets.cpp src/ports/reactor.cpp
    src/terms/term_text.cpp src/terms/term_text_scalars.cpp
    src/terms/atoms.cpp src/terms/atom_spelling.cpp src/terms/factory.cpp src/terms/immediate.cpp src/terms/term.cpp src/terms/equality.cpp src/terms/immediate_order.cpp src/terms/immediate_services.cpp
    src/builtins/registry.cpp src/builtins/invocation.cpp src/builtins/bridge.cpp src/builtins/portions.cpp src/builtins/float_text.cpp src/builtins/text.cpp src/builtins/typed.cpp src/builtins/io.cpp src/builtins/io_format.cpp src/builtins/io_pretty.cpp src/builtins/os.cpp src/builtins/math.cpp src/builtins/maps.cpp src/builtins/persistent_term.cpp src/builtins/code.cpp src/builtins/init.cpp src/builtins/atomics.cpp src/builtins/erts_internal.cpp src/builtins/output.cpp
    # Module erlang: one translation unit per builtin family (docs/builtins.md#source-layout).
    src/builtins/erlang/core.cpp src/builtins/erlang/term_access.cpp src/builtins/erlang/lists.cpp src/builtins/erlang/conversions.cpp src/builtins/erlang/conversion_text.cpp src/builtins/erlang/binary_conversions.cpp src/builtins/erlang/term_encoding.cpp src/builtins/erlang/term_decoding.cpp src/builtins/erlang/time.cpp src/builtins/erlang/timers.cpp src/builtins/erlang/introspection.cpp src/builtins/erlang/system.cpp src/builtins/erlang/spawning.cpp src/builtins/erlang/fun_info.cpp src/builtins/erlang/hashing.cpp src/builtins/erlang/md5.cpp src/builtins/erlang/node.cpp src/builtins/erlang/code.cpp src/builtins/erlang/processes.cpp src/builtins/erlang/ports.cpp src/builtins/erlang/dictionary.cpp
    # Module binary (plan step 41W).
    src/builtins/binary/binary_search.cpp src/builtins/binary/search.cpp src/builtins/binary/parts.cpp src/builtins/binary/encoding.cpp
    # Module unicode (plan step 41Y1).
    src/builtins/unicode/encodings.cpp src/builtins/unicode/properties.cpp src/builtins/unicode/tables.cpp src/builtins/unicode/chardata.cpp src/builtins/unicode/conversions.cpp
    # Module re over PCRE2 (plan step 41ZA).
    src/builtins/re/version.cpp src/builtins/re/options.cpp src/builtins/re/patterns.cpp src/builtins/re/matching.cpp
    # Module zlib streams (plan step 41Q).
    src/builtins/zlib/streams.cpp
    # Module lists (plan step 41T).
    src/builtins/lists/list_walk.cpp src/builtins/lists/sublists.cpp src/builtins/lists/ordering.cpp src/builtins/lists/keys.cpp src/builtins/lists/merges.cpp src/builtins/lists/zips.cpp
    # Modules ordsets, orddict and proplists (plan 21 step LIB1).
    src/builtins/collections/collections.cpp src/builtins/collections/ordsets.cpp src/builtins/collections/orddict.cpp src/builtins/collections/proplists.cpp
    # Modules dict and sets (plan 21 step LIB2).
    src/builtins/collections/linear_hash.cpp src/builtins/collections/dict.cpp src/builtins/collections/sets.cpp
    # Modules gb_trees, gb_sets and queue (plan 21 step LIB3).
    src/builtins/collections/balanced_tree.cpp src/builtins/collections/gb_trees.cpp src/builtins/collections/gb_sets.cpp src/builtins/collections/queue.cpp
    # Module array (plan 21 step LIB3B).
    src/builtins/collections/array.cpp
    src/startup/startup.cpp src/startup/arguments.cpp src/startup/options.cpp
    src/startup/halt.cpp
    src/modules/atoms.cpp src/modules/services.cpp src/modules/code_server.cpp src/modules/registration.cpp src/modules/literal_areas.cpp)
# List review headers for IDE navigation; they are not separate compilation units.
target_sources(clause_runtime PRIVATE
    include/atom_storage.hpp
    include/base_types.hpp
    include/callable.hpp
    include/code_server.hpp
    include/mailbox.hpp
    include/process_dictionary.hpp
    include/signals.hpp
    include/process.hpp
    include/process_heap.hpp
    include/scheduler.hpp
    src/terms/term_layout.hpp
    include/clause/runtime/base_types.hpp
    include/clause/runtime/terms.hpp
    include/clause/runtime/process_state.hpp
    include/clause/runtime/scheduler.hpp
    include/terms.hpp
)
set_target_properties(clause_runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
clause_project_options(clause_runtime)
# One section per function/object lets -Os executables drop unused runtime code at link time.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(clause_runtime PRIVATE /Gy /Gw)
else()
    target_compile_options(clause_runtime PRIVATE -ffunction-sections -fdata-sections)
endif()
# Runtime headers use Multiprecision, so consumers need the same header-only dependency.
target_include_directories(clause_runtime PUBLIC include)
# The I/O service of ports is platform specific (docs/ports.md#io-thread).
if(WIN32)
    target_sources(clause_runtime PRIVATE src/ports/io_windows.cpp src/ports/spawn_windows.cpp)
    # Windows 10 is the oldest supported version; Asio asks for it explicitly.
    target_compile_definitions(clause_runtime PRIVATE _WIN32_WINNT=0x0A00)
else()
    target_sources(clause_runtime PRIVATE src/ports/io_posix.cpp src/ports/spawn_posix.cpp)
endif()
# Every source that includes Boost.Asio sees the same configuration (one definition of each inline function).
target_compile_definitions(clause_runtime PRIVATE BOOST_ASIO_NO_DEPRECATED)
# Scheduler workers are std::threads; glibc before 2.34 needs -pthread (a plain flag keeps the export self-contained).
# Sockets use Boost.Asio (header-only, from the same Boost), which needs Winsock on Windows.
target_link_libraries(clause_runtime
    PUBLIC clause_multiprecision_dependency clause_abi $<$<PLATFORM_ID:Linux>:-pthread>
    $<$<PLATFORM_ID:Windows>:ws2_32> $<$<PLATFORM_ID:Windows>:mswsock>
)

# PCRE2 for the re builtins (plan step 41ZA): built from source into the archive itself, or an installed library
# whose path clau adds when it links an executable. clause_runtime.link beside the archive lists such extra linker
# inputs, one per line (docs/compile.md#pcre2).
clause_add_pcre2(clause_runtime)
# zlib for the zlib module and compressed external terms (plan step 41Q), always from the pinned source.
clause_add_runtime_zlib(clause_runtime)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:clause_runtime>/clause_runtime.link"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:clause_runtime,CLAUSE_LINK_ARGUMENTS>,\n>")

# Every generated-program consumer links one matching target runtime through this boundary.
# Platform dependencies (threads) come with clause_runtime.
add_library(clause_generated_program INTERFACE)
target_link_libraries(clause_generated_program INTERFACE clause_runtime)
add_library(Clause::generated_program ALIAS clause_generated_program)
# Exported for nested test programs as Clause::generated_program and Clause::runtime.
set_target_properties(clause_generated_program PROPERTIES EXPORT_NAME generated_program)
set_target_properties(clause_runtime PROPERTIES EXPORT_NAME runtime)
