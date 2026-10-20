include(${PROJECT_SOURCE_DIR}/cmake/CompilerDependencies.cmake)
# Optimized compiler (CLAUSE_OPTIMIZED_COMPILER): Debug keeps its debug information, the later flag wins over /Od.
if(CLAUSE_OPTIMIZED_COMPILER)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        add_compile_options("$<$<CONFIG:Debug>:/O2;/Ob2>")
    else()
        add_compile_options("$<$<CONFIG:Debug>:-O2>")
    endif()
endif()
add_library(clause_frontend STATIC src/parsing/probe.cpp src/parsing/token_cursor.cpp
    src/parsing/token_syntax.cpp src/parsing/operator_info.cpp
    src/parsing/delimiters.cpp src/parser/diagnostics.cpp
    src/ast/module.cpp src/ast/builder.cpp src/ast/children.cpp src/ast/clauses.cpp
    src/ast/attributes.cpp src/parser/attributes.cpp src/parser/attribute_terms.cpp
    src/parser/term_value.cpp src/parser/term_bits.cpp src/parser/declarations.cpp src/parser/documentation.cpp
    src/printing/tree_attributes.cpp
    src/ast/types.cpp src/parser/types.cpp src/parser/type_primary.cpp src/parser/type_names.cpp src/parser/type_structural.cpp
    src/printing/tree_types.cpp
    src/parser/specifications.cpp src/ast/specifications.cpp src/printing/tree_specifications.cpp
    src/ast/control.cpp src/parser/control.cpp src/printing/tree_control.cpp
    src/ast/exceptions.cpp src/parser/funs.cpp src/parser/exceptions.cpp src/printing/tree_exceptions.cpp
    src/ast/comprehensions.cpp src/parser/comprehensions.cpp src/printing/tree_comprehensions.cpp
    src/parser/parser.cpp src/parser/forms.cpp src/parser/literals.cpp src/parser/aggregates.cpp src/parser/expressions.cpp src/parser/clauses.cpp src/parser/maps.cpp src/parser/records.cpp src/parser/structural.cpp src/parser/binaries.cpp
    src/source/source.cpp src/diagnostics/diagnostic.cpp
    src/printing/source.cpp src/printing/token_text.cpp src/printing/printable.cpp src/printing/source_text.cpp
    src/printing/source_forms.cpp src/printing/source_expressions.cpp src/printing/source_control.cpp
    src/printing/source_types.cpp
    src/printing/tree.cpp src/printing/tree_forms.cpp src/printing/tree_expressions.cpp src/printing/tree_structural.cpp
    src/lexer/lexer.cpp src/lexer/numbers.cpp src/lexer/literals.cpp
    src/preprocessor/directives.cpp src/preprocessor/preprocessor.cpp
    src/preprocessor/token_utils.cpp src/preprocessor/arguments.cpp src/preprocessor/macros.cpp
    src/preprocessor/integer.cpp src/preprocessor/terms.cpp src/preprocessor/value.cpp src/preprocessor/expression_parse.cpp src/preprocessor/expression.cpp
    src/preprocessor/operators.cpp src/preprocessor/guards.cpp src/preprocessor/bits.cpp
    src/preprocessor/session.cpp src/preprocessor/conditions.cpp src/preprocessor/includes.cpp
    src/preprocessor/builtins.cpp src/preprocessor/features.cpp src/preprocessor/applications.cpp)
clause_project_options(clause_frontend)
target_include_directories(clause_frontend PUBLIC include PRIVATE src)
target_link_libraries(clause_frontend PRIVATE clause_parser_dependency clause_abi)

# Added for parse transforms: abstract-format terms and their codecs, the host OTP loader and form import.
add_library(clause_transforms STATIC src/transforms/terms.cpp src/transforms/etf.cpp src/transforms/term_text.cpp
    src/transforms/export.cpp src/transforms/export_expressions.cpp src/transforms/export_control.cpp
    src/transforms/export_forms.cpp src/transforms/export_types.cpp src/transforms/import.cpp
    src/transforms/import_expressions.cpp src/transforms/import_control.cpp src/transforms/import_forms.cpp
    src/transforms/import_types.cpp src/transforms/abstract_input.cpp src/transforms/annotations.cpp
    src/transforms/loader.cpp src/transforms/pipeline.cpp)
clause_project_options(clause_transforms)
target_include_directories(clause_transforms PRIVATE src)
target_link_libraries(clause_transforms PUBLIC clause_frontend PRIVATE clause_multiprecision_dependency clause_abi
    clause_llvm_sdk)
# The loader module is embedded into clau; erl compiles it in memory on each run (docs/transforms.md#loader).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/transforms/loader/clause_transform_loader.erl" LOADER_TEXT)
configure_file(src/transforms/loader/loader_source.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/transforms/loader_source.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/transforms/loader/clause_transform_loader.erl)
target_include_directories(clause_transforms PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Added for build caches (62D2): the cache store; its SQLite backend since 62D5.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cmake/Dependencies.cmake")
add_library(clause_cache STATIC src/cache/store.cpp src/cache/sqlite.cpp src/cache/blob_files.cpp
    src/cache/sqlite_store.cpp)
clause_project_options(clause_cache)
target_link_libraries(clause_cache PRIVATE clause_llvm_sdk clause_sqlite)

add_executable(clau src/main.cpp src/driver/command.cpp src/driver/options.cpp src/driver/frontend.cpp
    src/driver/build_cache.cpp src/driver/compile_cache.cpp src/driver/inference_cache.cpp src/driver/library_modules.cpp
    src/driver/backend_options.cpp src/driver/analysis.cpp src/driver/entry.cpp src/driver/escript.cpp src/driver/predefined.cpp src/driver/transform_sources.cpp src/driver/backend.cpp src/driver/compile_times.cpp src/driver/publication.cpp src/driver/project_backend.cpp src/driver/progress.cpp src/driver/display.cpp src/driver/inspection.cpp src/driver/type_report.cpp src/driver/environment.cpp
    src/driver/placeholders.cpp src/driver/stage_times.cpp)
if(WIN32 AND MSVC)
    # Keep narrow argv and filesystem conversions consistent with UTF-8 project paths.
    target_sources(clau PRIVATE clau.manifest)
endif()
set_target_properties(clau PROPERTIES
    OUTPUT_NAME clau
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
clause_project_options(clau)
target_compile_definitions(clau PRIVATE CLAUSE_VERSION="${PROJECT_VERSION}")
target_link_libraries(clau PRIVATE clause_abi clause_transforms clause_cache)

add_subdirectory(src/project)

target_link_libraries(clau PRIVATE clause_project)

# Keep LLVM headers and representation private behind the shared compilation backend.
add_library(clause_codegen STATIC src/codegen/sdk.cpp src/codegen/digest.cpp src/codegen/compilation.cpp src/codegen/module_atoms.cpp src/codegen/module_registration.cpp src/codegen/startup.cpp src/codegen/lowering_match.cpp src/codegen/lowering_immediates.cpp src/codegen/lowering_walk.cpp src/codegen/lowering_comprehensions.cpp src/codegen/lowering_fun_inlining.cpp src/codegen/lowering_list_loops.cpp src/codegen/lowering_map_loops.cpp src/codegen/lowering_integer_text.cpp src/codegen/lowering_guards.cpp
    src/codegen/specialization.cpp src/codegen/specialization_analysis.cpp src/codegen/integer_guards.cpp
    src/codegen/specialization_cloning.cpp src/codegen/specialization_dispatch.cpp src/codegen/specialization_lowering.cpp
    src/codegen/result.cpp src/codegen/diagnostics.cpp src/codegen/target.cpp
    src/codegen/target_backends.cpp src/codegen/verification.cpp src/codegen/emission.cpp src/codegen/optimization.cpp src/codegen/serialization.cpp src/codegen/progress.cpp src/codegen/specialization_trace.cpp src/codegen/term_abi.cpp src/codegen/features.cpp src/codegen/lowering_boundaries.cpp src/codegen/lowering.cpp src/codegen/lowering_expressions.cpp src/codegen/lowering_containers.cpp src/codegen/lowering_append.cpp src/codegen/lowering_records.cpp src/codegen/lowering_native_records.cpp src/codegen/lowering_record_tests.cpp src/codegen/lowering_integers.cpp src/codegen/lowering_floats.cpp src/codegen/lowering_float_arithmetic.cpp src/codegen/lowering_bits.cpp src/codegen/lowering_bit_construction.cpp src/codegen/lowering_maps.cpp src/codegen/lowering_map_access.cpp src/codegen/lowering_roots.cpp src/codegen/lowering_clauses.cpp src/codegen/binary_loops.cpp src/codegen/alias_tags.cpp src/codegen/float_loops.cpp src/codegen/lowering_proofs.cpp src/codegen/lowering_inline.cpp src/codegen/literals.cpp src/codegen/proofs.cpp src/codegen/lowering_body_match.cpp src/codegen/lowering_calls.cpp src/codegen/lowering_funs.cpp src/codegen/frames.cpp
    src/codegen/limits.cpp src/codegen/bounded_stream.cpp src/codegen/source_annotations.cpp src/codegen/source_locations.cpp
    src/codegen/assembly_listing.cpp src/codegen/assembly_annotations.cpp)
clause_project_options(clause_codegen)
target_link_libraries(clause_codegen PRIVATE clause_llvm_sdk clause_frontend clause_abi)
get_target_property(codegen_backends clause_llvm_sdk CLAUSE_LLVM_BACKENDS)
foreach(backend IN LISTS codegen_backends)
    target_compile_definitions(clause_codegen PRIVATE "CLAUSE_LLVM_${backend}=1")
endforeach()
target_link_libraries(clau PRIVATE clause_codegen)

# Semantic analysis owns language identities without depending on LLVM representations.
add_library(clause_semantic STATIC
    src/semantic/declarations.cpp src/semantic/behaviours.cpp src/semantic/module_info.cpp src/semantic/imports.cpp src/semantic/erlang_module.cpp src/semantic/escript.cpp src/semantic/records.cpp src/semantic/native_records.cpp src/semantic/record_definitions.cpp src/semantic/funs.cpp src/semantic/symbols.cpp
    src/semantic/bindings.cpp src/semantic/map_ownership.cpp src/semantic/calls.cpp src/semantic/liveness.cpp src/semantic/match_plan.cpp src/semantic/match_plan_containers.cpp src/semantic/match_plan_records.cpp src/semantic/match_plan_bits.cpp src/semantic/binary_options.cpp src/semantic/match_plan_maps.cpp src/semantic/pattern_reads.cpp
    src/semantic/binding_patterns.cpp src/semantic/binding_expressions.cpp
    src/semantic/pattern_analysis.cpp src/semantic/pattern_constants.cpp
    src/semantic/pattern_expressions.cpp src/semantic/pattern_binary.cpp
    src/semantic/pattern_calls.cpp
    src/semantic/guard_analysis.cpp src/semantic/immediate_services.cpp src/semantic/service_metadata.cpp
    src/semantic/features.cpp src/semantic/capabilities.cpp
    src/semantic/literals.cpp src/semantic/comprehensions.cpp src/semantic/expression_capability.cpp
    src/semantic/types/domain.cpp src/semantic/types/syntax.cpp
    src/semantic/types/declarations.cpp src/semantic/types/collect.cpp
    src/semantic/types/resolver.cpp src/semantic/types/traversal.cpp
    src/semantic/types/constants.cpp src/semantic/types/expansion.cpp
    src/semantic/types/inference.cpp src/semantic/types/inference_bindings.cpp
    src/semantic/types/inference_values.cpp
    src/semantic/types/inference_operators.cpp
    src/semantic/types/inference_containers.cpp
    src/semantic/types/inference_funs.cpp
    src/semantic/types/inference_inputs.cpp
    src/semantic/types/inference_narrowing.cpp src/semantic/types/inference_scopes.cpp
    src/semantic/types/meet.cpp src/semantic/types/inference_uses.cpp src/semantic/types/function_types.cpp
    src/semantic/types/dependent.cpp
    src/semantic/types/inference_apply.cpp
    src/semantic/types/inference_store.cpp
    src/semantic/types/contracts.cpp
    src/semantic/types/printing.cpp
    src/semantic/types/lattice.cpp)
clause_project_options(clause_semantic)
target_include_directories(clause_semantic PRIVATE src)
target_link_libraries(clause_semantic PUBLIC clause_frontend clause_abi)
target_link_libraries(clau PRIVATE clause_semantic)
target_link_libraries(clause_codegen PRIVATE clause_semantic)

add_library(clause_artifacts STATIC src/artifacts/artifacts.cpp src/artifacts/paths.cpp src/artifacts/replace.cpp)
clause_project_options(clause_artifacts)
target_include_directories(clause_artifacts PRIVATE src)
target_link_libraries(clause_artifacts PRIVATE clause_project clause_semantic)
target_link_libraries(clau PRIVATE clause_artifacts)

# Executable linking drives Clang with the module/startup objects and the matching runtime archive.
add_library(clause_linking STATIC src/linking/link.cpp src/linking/toolchain.cpp src/linking/runtime_library.cpp)
clause_project_options(clause_linking)
target_include_directories(clause_linking PRIVATE src)
target_link_libraries(clause_linking PRIVATE clause_llvm_sdk clause_artifacts clause_project)
# The default runtime archive path is relative to clau and follows this build tree's layout.
if(CLAUSE_BUILD_RUNTIME)
    set(default_runtime "$<PATH:RELATIVE_PATH,$<TARGET_FILE:clause_runtime>,$<TARGET_FILE_DIR:clau>>")
else()
    set(default_runtime "../lib/${CMAKE_STATIC_LIBRARY_PREFIX}clause_runtime${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
target_compile_definitions(clause_linking PRIVATE "CLAUSE_DEFAULT_RUNTIME=\"${default_runtime}\"")
# Library modules (lists, maps) compile from source with the programs that call them.
set(default_library "$<PATH:RELATIVE_PATH,${PROJECT_SOURCE_DIR}/library,$<TARGET_FILE_DIR:clau>>")
target_compile_definitions(clause_linking PRIVATE "CLAUSE_DEFAULT_LIBRARY=\"${default_library}\"")
target_link_libraries(clau PRIVATE clause_linking)
