include(${PROJECT_SOURCE_DIR}/cmake/BoostDependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Pcre2Dependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/ZlibDependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/ZstdDependencies.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/OpenSSLDependencies.cmake)
option(CLAUSE_RUNTIME_CRYPTO "Build the crypto runtime archive over OpenSSL (plan 21 step LIB79, docs/libraries/crypto.md)" ON)

add_library(clause_runtime STATIC src/runtime.cpp src/diagnostics/features.cpp
    src/terms/bit_factory.cpp src/terms/bit_access.cpp src/terms/word_order.cpp src/terms/bit_float.cpp src/terms/bit_numeric.cpp src/terms/bit_utf.cpp src/terms/bit_services.cpp
    src/terms/map_factory.cpp src/terms/map_access.cpp src/terms/map_services.cpp src/terms/map_tree.cpp src/terms/map_cursor.cpp
    src/terms/records.cpp src/terms/record_services.cpp src/terms/funs.cpp src/terms/fun_services.cpp src/terms/identities.cpp src/terms/dynamic_calls.cpp
    src/terms/float_factory.cpp src/terms/float_literal.cpp src/terms/float_operations.cpp src/terms/numeric_conversions.cpp src/terms/numeric_order.cpp src/terms/numeric_service.cpp
    src/terms/integer_values.cpp src/terms/integer_factory.cpp src/terms/integer_operations.cpp src/terms/integer_access.cpp src/terms/integer_literal.cpp
    src/terms/integer_service.cpp src/terms/integer_decimal.cpp src/terms/integer_sum.cpp src/terms/integer_words.cpp src/terms/integer_scale.cpp
    src/memory/heap_terms.cpp src/memory/heap_publication.cpp
    src/terms/container_access.cpp src/terms/container_factory.cpp src/terms/container_services.cpp src/terms/structural_order.cpp
    src/process/generated_calls.cpp src/process/exceptions.cpp src/process/exits.cpp src/process/receive.cpp src/process/stack.cpp src/process/profile.cpp src/process/call_trace.cpp src/process/context.cpp src/process/identities.cpp src/process/ownership.cpp src/process/storage.cpp
    src/memory/heap.cpp src/memory/heap_storage.cpp src/memory/heap_blocks.cpp src/memory/heap_reservation.cpp src/memory/off_heap.cpp src/memory/heap_collect.cpp src/memory/heap_walk.cpp src/memory/heap_verify.cpp src/memory/copy.cpp src/memory/persistent_region.cpp src/memory/persistent_terms.cpp
    src/scheduler/services.cpp src/scheduler/registry.cpp src/scheduler/transitions.cpp src/scheduler/executor.cpp src/scheduler/cpus.cpp src/scheduler/timer_wheel.cpp src/scheduler/timers.cpp src/scheduler/introspection.cpp src/scheduler/suspension.cpp src/scheduler/aliases.cpp src/scheduler/signals.cpp src/scheduler/ports.cpp src/ports/fd.cpp src/ports/input.cpp src/ports/io.cpp src/ports/spawn.cpp src/ports/file.cpp src/ports/posix_reason.cpp src/ports/file_info.cpp src/ports/file_paths.cpp src/ports/file_changes.cpp src/ports/value.cpp src/ports/sockets.cpp src/ports/socket_options.cpp src/ports/reactor.cpp
    src/terms/term_text.cpp src/terms/term_text_scalars.cpp
    src/terms/atoms.cpp src/terms/atom_table.cpp src/terms/atom_spelling.cpp src/terms/factory.cpp src/terms/immediate.cpp src/terms/term.cpp src/terms/term_unchecked.cpp src/terms/equality.cpp src/terms/immediate_order.cpp src/terms/immediate_services.cpp
    src/builtins/registry.cpp src/builtins/invocation.cpp src/builtins/bridge.cpp src/builtins/portions.cpp src/builtins/float_text.cpp src/builtins/text.cpp src/builtins/typed.cpp src/builtins/io.cpp src/builtins/io_format.cpp src/builtins/os.cpp src/builtins/math.cpp src/builtins/maps.cpp src/builtins/persistent_term.cpp src/builtins/code.cpp src/builtins/interpreted.cpp src/builtins/init.cpp src/builtins/atomics.cpp src/builtins/erts_internal.cpp src/builtins/output.cpp
    # Module erlang: one translation unit per builtin family (docs/builtins.md#source-layout).
    src/builtins/erlang/core.cpp src/builtins/erlang/term_access.cpp src/builtins/erlang/lists.cpp src/builtins/erlang/conversions.cpp src/builtins/erlang/conversion_text.cpp src/builtins/erlang/binary_conversions.cpp src/builtins/erlang/term_encoding.cpp src/builtins/erlang/term_decoding.cpp src/builtins/erlang/time.cpp src/builtins/erlang/timers.cpp src/builtins/erlang/introspection.cpp src/builtins/erlang/system.cpp src/builtins/erlang/spawning.cpp src/builtins/erlang/fun_info.cpp src/builtins/erlang/apply.cpp src/builtins/erlang/hashing.cpp src/builtins/erlang/md5.cpp src/builtins/erlang/node.cpp src/builtins/erlang/code.cpp src/builtins/erlang/processes.cpp src/builtins/erlang/ports.cpp src/builtins/erlang/dictionary.cpp
    # Module binary (plan step 41W).
    src/builtins/binary/binary_search.cpp src/builtins/binary/search.cpp src/builtins/binary/parts.cpp src/builtins/binary/encoding.cpp
    # Module unicode (plan step 41Y1).
    src/builtins/unicode/encodings.cpp src/builtins/unicode/properties.cpp src/builtins/unicode/tables.cpp src/builtins/unicode/chardata.cpp src/builtins/unicode/conversions.cpp
    # Module re over PCRE2 (plan step 41ZA).
    src/builtins/re/version.cpp src/builtins/re/options.cpp src/builtins/re/patterns.cpp src/builtins/re/matching.cpp
    # Module zlib streams (plan step 41Q).
    src/builtins/zlib/streams.cpp
    # Module zstd contexts and dictionaries (plan 21 step LIB54E).
    src/builtins/zstd/contexts.cpp
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
    # Module calendar (plan 21 step LIB5; runtime/src/builtins/calendar.cpp).
    src/builtins/calendar.cpp
    # Module timer's conversions (plan 21 step LIB22).
    src/builtins/timer.cpp
    # Module base64 (plan 21 step LIB5; runtime/src/builtins/encoding/).
    src/builtins/encoding/base64.cpp
    # Module clause_asn1, the codec of generated ASN.1 modules (plan 21 step LIB84; runtime/src/builtins/asn1/).
    src/builtins/asn1/common.cpp src/builtins/asn1/tlv.cpp src/builtins/asn1/ber_decode.cpp
    src/builtins/asn1/ber_constructed.cpp src/builtins/asn1/ber_encode.cpp src/builtins/asn1/ber_encode_constructed.cpp
    src/builtins/asn1/values.cpp src/builtins/asn1/legacy.cpp src/builtins/asn1/builtins.cpp
    # Packed encoding rules (plan 21 step LIB84).
    src/builtins/asn1/per_bits.cpp src/builtins/asn1/per_strings.cpp src/builtins/asn1/per_encode.cpp
    src/builtins/asn1/per_encode_constructed.cpp src/builtins/asn1/per_decode.cpp
    src/builtins/asn1/per_decode_constructed.cpp
    # Module clause_public_key (plan 21 step LIB80B).
    src/builtins/public_key/translate.cpp src/builtins/public_key/tables.cpp src/builtins/public_key/builtins.cpp
    # Trust stores (plan 21 step LIB80D6).
    src/builtins/public_key/os_cacerts.cpp
    # PEM text and PBE tables (plan 21 step LIB80E1).
    src/builtins/public_key/pem.cpp src/builtins/public_key/pbe.cpp
    # Calls of other builtins (plan 21 step LIB80E5).
    src/builtins/public_key/calls.cpp
    # The policy tree (plan 21 step LIB80E4).
    src/builtins/public_key/policy.cpp
    # Module ssl's cipher suites (plan 21 step LIB81A).
    src/builtins/ssl/suites.cpp src/builtins/ssl/suite_names.cpp src/builtins/ssl/suite_parse.cpp src/builtins/ssl/hello.cpp
    src/builtins/ssl/builtins.cpp
    # Module json (plan 21 step LIB5E).
    src/builtins/encoding/json.cpp src/builtins/encoding/json_encode.cpp
    # Module filename (plan 21 step LIB5G).
    src/builtins/filename/filename.cpp src/builtins/filename/filename_join.cpp
    # Module uri_string and grapheme clusters (plan 21 step LIB5G).
    src/builtins/uri/uri_common.cpp src/builtins/uri/uri_parse.cpp src/builtins/uri/uri_compose.cpp
    src/builtins/uri/uri_codec.cpp src/builtins/unicode/graphemes.cpp
    # Module string (plan 21 step LIB4): chardata code points and clusters in OTP's shapes.
    src/builtins/unicode/code_points.cpp src/builtins/unicode/normalize.cpp src/builtins/unicode/forms.cpp
    src/builtins/string/string_common.cpp src/builtins/string/string_text.cpp
    src/builtins/string/string_search.cpp src/builtins/string/string_lexemes.cpp
    src/builtins/string/string_numbers.cpp src/builtins/string/string_latin1.cpp
    # Module io_lib (plan 21 steps LIB4C, LIB4D, LIB4E): deep character lists, writers, the format engine and io_lib_pretty.
    src/builtins/io_lib/deep.cpp src/builtins/io_lib/write.cpp src/builtins/io_lib/write_builtins.cpp
    src/builtins/io_lib/format_scan.cpp src/builtins/io_lib/format_build.cpp src/builtins/io_lib/format_float.cpp
    src/builtins/io_lib/format_builtins.cpp src/builtins/io_lib/collect.cpp src/builtins/io_lib/fread.cpp src/builtins/io_lib/pretty.cpp
    src/builtins/io_lib/pretty_length.cpp src/builtins/io_lib/pretty_layout.cpp src/builtins/io_lib/pretty_bin.cpp
    # maps_order comparison funs (plan 21 step LIB4F).
    src/builtins/io_lib/key_orders.cpp
    # Module erl_posix_msg (plan 21 step LIB50).
    src/builtins/erl_posix_msg.cpp
    # Module clause_io: terminal queries of io and io_ansi (plan 21 step LIB63).
    src/builtins/terminal.cpp
    # Module clause_net: names and interfaces of net and inet (plan 21 step LIB60).
    src/builtins/net/names.cpp src/builtins/net/interfaces.cpp src/builtins/net/interfaces_windows.cpp
    src/builtins/net/interfaces_posix.cpp src/builtins/net/dns.cpp
    # filelib's wildcard patterns (plan 21 step LIB51).
    src/builtins/filelib.cpp
    # argparse's validation, parser and help (plan 21 step LIB70).
    src/builtins/argparse/argparse.cpp src/builtins/argparse/validate.cpp src/builtins/argparse/parse.cpp
    src/builtins/argparse/help.cpp
    # Source quotes of compile's messages (plan 21 step LIB77E1).
    src/builtins/compile_messages.cpp
    # Module erl_syntax (plan 21 step LIB77E2).
    src/builtins/syntax/nodes.cpp src/builtins/syntax/accessors.cpp src/builtins/syntax/build.cpp
    src/builtins/syntax/lists.cpp src/builtins/syntax/attrs.cpp src/builtins/syntax/shapes.cpp
    src/builtins/syntax/revert.cpp src/builtins/syntax/literals.cpp
    # Module erl_syntax_lib (plan 21 step LIB77E4a).
    src/builtins/syntax_lib/analyze.cpp src/builtins/syntax_lib/bindings.cpp src/builtins/syntax_lib/trees.cpp
    # Modules erl_comment_scan and erl_recomment (plan 21 step LIB77E4b).
    src/builtins/comments/scan.cpp src/builtins/comments/recomment.cpp
    # epp_dodger's token and form rewriting (plan 21 step LIB77E4c).
    src/builtins/epp_dodger/tokens.cpp src/builtins/epp_dodger/rewrite.cpp
    # merl's templates and matching (plan 21 step LIB77E5).
    src/builtins/merl/shapes.cpp src/builtins/merl/templates.cpp src/builtins/merl/match.cpp
    # The qlc parse transform at run time (plan 21 step LIB73).
    src/builtins/qlc_pt/kit.cpp src/builtins/qlc_pt/state.cpp src/builtins/qlc_pt/messages.cpp
    src/builtins/qlc_pt/frames.cpp src/builtins/qlc_pt/columns.cpp src/builtins/qlc_pt/clauses.cpp
    src/builtins/qlc_pt/generate.cpp
    # prettypr's layout chooser (plan 21 step LIB77E3a).
    src/builtins/prettypr/documents.cpp src/builtins/prettypr/rewrite.cpp src/builtins/prettypr/floats.cpp
    src/builtins/prettypr/builtins.cpp
    # Modules erl_anno and erl_internal (plan 21 step LIB8).
    src/builtins/front_end/erl_anno.cpp src/builtins/front_end/erl_internal.cpp
    # Module erl_scan (plan 21 step LIB30).
    src/builtins/front_end/scanner.cpp src/builtins/front_end/scanner_numbers.cpp
    src/builtins/front_end/scanner_texts.cpp src/builtins/front_end/erl_scan.cpp
    # Module erl_parse (plan 21 step LIB31).
    src/builtins/front_end/abstract_annos.cpp src/builtins/front_end/abstract_normalise.cpp
    src/builtins/front_end/parser.cpp src/builtins/front_end/parser_attributes.cpp
    src/builtins/front_end/parser_expressions.cpp src/builtins/front_end/parser_primaries.cpp
    src/builtins/front_end/parser_control.cpp src/builtins/front_end/parser_types.cpp
    src/builtins/front_end/erl_parse.cpp src/builtins/front_end/bit_types.cpp
    # Module erl_eval (plan 21 step LIB32).
    src/builtins/front_end/erl_eval.cpp
    # ms_transform in C++ (C++ ms_transform).
    src/builtins/front_end/ms_head.cpp src/builtins/front_end/ms_guards.cpp src/builtins/front_end/ms_transform.cpp
    # C++ cores of the front-end modules (plan 21 step LIB34C).
    src/builtins/front_end/expand_records.cpp src/builtins/front_end/guard_tests.cpp
    src/builtins/front_end/lint_state.cpp src/builtins/front_end/lint_vars.cpp src/builtins/front_end/lint_forms.cpp
    src/builtins/front_end/lint_checks.cpp src/builtins/front_end/lint_exprs.cpp src/builtins/front_end/lint_scopes.cpp
    src/builtins/front_end/lint_patterns.cpp src/builtins/front_end/lint_records.cpp
    src/builtins/front_end/lint_types.cpp src/builtins/front_end/lint_format.cpp src/builtins/front_end/lint_eval.cpp
    src/builtins/front_end/lint_errors.cpp src/builtins/front_end/lint_builtins.cpp
    src/builtins/front_end/pp_items.cpp src/builtins/front_end/pp_exprs.cpp src/builtins/front_end/pp_forms.cpp
    src/builtins/front_end/pp_types.cpp src/builtins/front_end/pp_layout.cpp src/builtins/front_end/pp_builtins.cpp
    src/builtins/front_end/epp_eval.cpp src/builtins/front_end/guard_eval.cpp src/builtins/front_end/eval_match.cpp
    # The erlang gaps of plan 21 step LIB9A.
    src/builtins/erlang/checksums.cpp src/builtins/erlang/tuple_edits.cpp src/builtins/erlang/process_options.cpp
    src/builtins/erlang/system_flags.cpp src/builtins/erlang/packets.cpp src/builtins/erlang/packet_http.cpp
    src/builtins/erlang/suspension.cpp
    # Module sofs (plan 21 step LIB7A).
    src/builtins/sofs/callbacks.cpp src/builtins/sofs/common.cpp src/builtins/sofs/constructors.cpp
    src/builtins/sofs/families.cpp src/builtins/sofs/family_ops.cpp src/builtins/sofs/normalize.cpp
    src/builtins/sofs/products.cpp src/builtins/sofs/relations.cpp src/builtins/sofs/selectors.cpp
    src/builtins/sofs/sets.cpp src/builtins/sofs/set_ops.cpp src/builtins/sofs/types.cpp
    # Module graph (plan 21 step LIB7B).
    src/builtins/graph/traversal.cpp src/builtins/graph/paths.cpp src/builtins/graph/queries.cpp
    src/builtins/graph/traversal_builtins.cpp
    # ETS tables and module ets (plan 21 step LIB10).
    src/ets/index.cpp src/ets/hash_index.cpp src/ets/tree_index.cpp src/ets/table.cpp src/ets/tables.cpp
    src/scheduler/tables.cpp src/memory/detached_term.cpp
    src/builtins/ets/common.cpp src/builtins/ets/options.cpp src/builtins/ets/tables.cpp src/builtins/ets/info.cpp
    src/builtins/ets/writes.cpp src/builtins/ets/reads.cpp src/builtins/ets/walks.cpp src/builtins/ets/updates.cpp
    # Match specifications and the ets select family (plan 21 step LIB11).
    src/match_spec/compile.cpp src/match_spec/patterns.cpp src/match_spec/expressions.cpp src/match_spec/functions.cpp
    src/match_spec/run.cpp src/match_spec/actions.cpp src/match_spec/handles.cpp
    src/builtins/ets/select_common.cpp src/builtins/ets/select.cpp src/builtins/ets/select_edits.cpp
    src/builtins/ets/match_specs.cpp
    # ETS operations in portions (plan 21 step LIB10F).
    src/builtins/ets/disposal.cpp src/builtins/ets/table_walks.cpp
    # Modules digraph and digraph_utils (plan 21 step LIB12).
    src/builtins/digraph/view.cpp src/builtins/digraph/builtins.cpp
    # Halt log files of disk_log's internal format for ets table files (plan 21 step LIB12).
    src/builtins/disk_log/halt_log.cpp
    # The dets file engine (plan 21 step LIB53).
    src/builtins/dets/format.cpp src/builtins/dets/file.cpp src/builtins/dets/free_lists.cpp src/builtins/dets/header.cpp src/builtins/dets/objects.cpp src/builtins/dets/table.cpp src/builtins/dets/growth.cpp src/builtins/dets/work_list.cpp src/builtins/dets/scan.cpp src/builtins/dets/bulk.cpp src/builtins/dets/open.cpp src/builtins/dets/builtins.cpp
    # Modules erl_features and records (plan 21 step LIB8B).
    src/builtins/front_end/features.cpp src/builtins/records.cpp
    # Modules rand and random (plan 21 step LIB6).
    src/builtins/rand/generators.cpp src/builtins/rand/rand.cpp src/builtins/rand/tables.cpp
    src/startup/startup.cpp src/startup/arguments.cpp src/startup/options.cpp
    src/startup/halt.cpp
    src/modules/atoms.cpp src/modules/literal_heap.cpp src/modules/services.cpp src/modules/code_server.cpp src/modules/interpreted_modules.cpp src/modules/registration.cpp src/modules/literal_areas.cpp)
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
    # The registry driver of win32reg (plan 21 step LIB55B) exists on Windows only.
    target_sources(clause_runtime PRIVATE src/ports/io_windows.cpp src/ports/spawn_windows.cpp
        src/ports/registry_windows.cpp)
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
    $<$<PLATFORM_ID:Windows>:ws2_32> $<$<PLATFORM_ID:Windows>:mswsock> $<$<PLATFORM_ID:Windows>:advapi32>
    $<$<PLATFORM_ID:Windows>:iphlpapi> $<$<PLATFORM_ID:Windows>:crypt32>
)

# PCRE2 for the re builtins (plan step 41ZA): built from source into the archive itself, or an installed library
# whose path clau adds when it links an executable. clause_runtime.link beside the archive (named after it:
# libclause_runtime.link beside libclause_runtime.a) lists such extra linker inputs, one per line
# (docs/compile.md#pcre2).
clause_add_pcre2(clause_runtime)
# zlib for the zlib module and compressed external terms (plan step 41Q), always from the pinned source.
clause_add_runtime_zlib(clause_runtime)
# zstd for the zstd module (plan 21 step LIB54E), always from the pinned source.
clause_add_runtime_zstd(clause_runtime)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:clause_runtime>/$<TARGET_FILE_PREFIX:clause_runtime>clause_runtime.link"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:clause_runtime,CLAUSE_LINK_ARGUMENTS>,\n>")

# The crypto runtime archive (plan 21 step LIB79): the crypto builtins over OpenSSL's libcrypto, beside
# clause_runtime. clau links it only into programs whose batch has the library crypto module (or --with-crypto).
# clause_runtime_crypto.link lists libcrypto, then its system libraries, one per line; clause_runtime_crypto.openssl
# records how this build configures an OpenSSL source tree, for clau --with-crypto=SOURCE.
if(CLAUSE_RUNTIME_CRYPTO)
    add_library(clause_runtime_crypto STATIC src/builtins/crypto/extension.cpp src/builtins/crypto/errors.cpp
        src/builtins/crypto/algorithms.cpp src/builtins/crypto/hash.cpp src/builtins/crypto/mac.cpp
        src/builtins/crypto/random.cpp src/builtins/crypto/numbers.cpp src/builtins/crypto/cipher.cpp
        src/builtins/crypto/aead.cpp src/builtins/crypto/fips.cpp src/builtins/crypto/params.cpp
        src/builtins/crypto/pkey.cpp src/builtins/crypto/keys.cpp src/builtins/crypto/curves.cpp
        src/builtins/crypto/srp.cpp)
    set_target_properties(clause_runtime_crypto PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        EXPORT_NAME crypto)
    clause_project_options(clause_runtime_crypto)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(clause_runtime_crypto PRIVATE /Gy /Gw)
    else()
        target_compile_options(clause_runtime_crypto PRIVATE -ffunction-sections -fdata-sections)
    endif()
    target_include_directories(clause_runtime_crypto PRIVATE src)
    target_link_libraries(clause_runtime_crypto PUBLIC clause_runtime)
    clause_add_openssl(clause_runtime_crypto)
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:clause_runtime_crypto>/$<TARGET_FILE_PREFIX:clause_runtime_crypto>clause_runtime_crypto.link"
        CONTENT "$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:clause_runtime_crypto,CLAUSE_OPENSSL_DEBUG>,$<TARGET_PROPERTY:clause_runtime_crypto,CLAUSE_OPENSSL_RELEASE>>\n$<JOIN:$<TARGET_PROPERTY:clause_runtime_crypto,CLAUSE_LINK_ARGUMENTS>,\n>\n")
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:clause_runtime_crypto>/$<TARGET_FILE_PREFIX:clause_runtime_crypto>clause_runtime_crypto.openssl"
        CONTENT "$<JOIN:$<TARGET_PROPERTY:clause_runtime_crypto,CLAUSE_OPENSSL_CONFIGURATION>,\n>\n")

    # The ssl runtime archive (plan 21 step LIB81B): the clause_ssl_engine builtins over libssl of the same OpenSSL.
    # clau links it, before the crypto archive, into programs whose batch has the library clause_ssl_engine module;
    # clause_runtime_ssl.link names libssl, which goes before libcrypto.
    add_library(clause_runtime_ssl STATIC src/builtins/ssl_engine/extension.cpp src/builtins/ssl_engine/engine.cpp
        src/builtins/ssl_engine/context.cpp src/builtins/ssl_engine/connection.cpp src/builtins/ssl_engine/info.cpp
        src/builtins/ssl_engine/features.cpp src/builtins/ssl_engine/sessions.cpp
        src/builtins/ssl_engine/tickets.cpp src/builtins/ssl_engine/early_data.cpp
        src/builtins/ssl_engine/sni.cpp src/builtins/ssl_engine/psk.cpp)
    set_target_properties(clause_runtime_ssl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        EXPORT_NAME ssl)
    clause_project_options(clause_runtime_ssl)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(clause_runtime_ssl PRIVATE /Gy /Gw)
    else()
        target_compile_options(clause_runtime_ssl PRIVATE -ffunction-sections -fdata-sections)
    endif()
    target_include_directories(clause_runtime_ssl PRIVATE src)
    target_link_libraries(clause_runtime_ssl PUBLIC clause_runtime_crypto)
    clause_add_libssl(clause_runtime_ssl clause_runtime_crypto)
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:clause_runtime_ssl>/$<TARGET_FILE_PREFIX:clause_runtime_ssl>clause_runtime_ssl.link"
        CONTENT "$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:clause_runtime_ssl,CLAUSE_OPENSSL_DEBUG>,$<TARGET_PROPERTY:clause_runtime_ssl,CLAUSE_OPENSSL_RELEASE>>\n")
endif()

# Every generated-program consumer links one matching target runtime through this boundary.
# Platform dependencies (threads) come with clause_runtime.
add_library(clause_generated_program INTERFACE)
target_link_libraries(clause_generated_program INTERFACE clause_runtime)
add_library(Clause::generated_program ALIAS clause_generated_program)
# Exported for nested test programs as Clause::generated_program and Clause::runtime.
set_target_properties(clause_generated_program PROPERTIES EXPORT_NAME generated_program)
set_target_properties(clause_runtime PROPERTIES EXPORT_NAME runtime)
