option(CLAUSE_OTP_AUDITS "Enable optional live OTP/reference audits (not required for normal tests)" OFF)
set(oracle_modes golden)
if(CLAUSE_OTP_AUDITS)
    include(${PROJECT_SOURCE_DIR}/cmake/ErlangDependencies.cmake)
    list(APPEND oracle_modes oracle)
endif()
set(CLAUSE_OTP_SOURCE_ROOT "${PROJECT_SOURCE_DIR}/references/otp" CACHE PATH
    "Optional pinned OTP maint-29 source checkout for parser reduction and real-source tests")
if(CLAUSE_OTP_AUDITS)
foreach(name IN ITEMS coverage corpus)
    add_test(NAME parser_${name} COMMAND "${CMAKE_COMMAND}"
        "-DOTP_ROOT=${CLAUSE_OTP_SOURCE_ROOT}" "-DTOOL=$<TARGET_FILE:clau>"
        "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser" "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/${name}.cmake")
    set_tests_properties(parser_${name} PROPERTIES TIMEOUT 120
        SKIP_REGULAR_EXPRESSION "SKIP: pinned OTP source checkout unavailable")
endforeach()
endif()
foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    foreach(suite IN ITEMS historical phase6)
        set(script historical.cmake)
        if(suite STREQUAL "phase6")
            set(script phase2.cmake)
        endif()
        add_test(NAME parser_${suite}_${mode} COMMAND "${CMAKE_COMMAND}"
            "-DPHASE=phase6" "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
            "-DDUMP=$<TARGET_FILE:parser_dump>" "-DESCRIPT=${CLAUSE_ESCRIPT}"
            "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript" "-DLIVE=${live}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/${script}")
        set_tests_properties(parser_${suite}_${mode} PROPERTIES TIMEOUT 90)
    endforeach()
endforeach()
foreach(name IN ITEMS hardening)
    add_executable(parser_${name} compiler/parser/${name}.cpp)
    clause_project_options(parser_${name})
    target_link_libraries(parser_${name} PRIVATE clause_frontend)
    add_test(NAME parser_${name} COMMAND parser_${name})
    set_tests_properties(parser_${name} PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME parser_consumer COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/parser-consumer/$<CONFIG>"
    "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake" "-DHOST_CONFIG=$<CONFIG>"
    "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/consumer.cmake")
# Separate CMake consumer projects and timing measurements run only in full mode (label full_only).
set_tests_properties(parser_consumer PROPERTIES TIMEOUT 180 LABELS full_only)
# The mutation corpus runs as four shards (every fourth case each) so CTest can run them in parallel.
foreach(shard RANGE 0 3)
    add_test(NAME parser_mutations_${shard} COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:clau>" "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/parser-mutations/${shard}" -DSHARD=${shard} -DSHARDS=4
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/mutations.cmake")
    set_tests_properties(parser_mutations_${shard} PROPERTIES TIMEOUT 180)
endforeach()
add_test(NAME parser_stress COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/parser-stress"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/stress.cmake")
set_tests_properties(parser_stress PROPERTIES TIMEOUT 90)

add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake"
)

add_test(NAME frontend_cli COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser/cli"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/frontend-cli" "-DWORD_BYTES=${CMAKE_SIZEOF_VOID_P}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/frontend_cases.cmake")
add_test(NAME printing_roundtrip COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DDUMP=$<TARGET_FILE:preprocessor_dump>"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/preprocessor/printing"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/printing-roundtrip"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/printing_roundtrip.cmake")
set_tests_properties(frontend_cli printing_roundtrip PROPERTIES TIMEOUT 90)
add_test(NAME preprocessor_workflow COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/preprocessor-workflow"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/workflow.cmake")
set_tests_properties(preprocessor_workflow PROPERTIES TIMEOUT 90)
add_executable(parser_control compiler/parser/control.cpp)
clause_project_options(parser_control)
target_include_directories(parser_control PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_control PRIVATE clause_frontend)
add_test(NAME parser_control COMMAND parser_control)
add_executable(parser_exceptions compiler/parser/exceptions.cpp)
clause_project_options(parser_exceptions)
target_include_directories(parser_exceptions PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_exceptions PRIVATE clause_frontend)
add_test(NAME parser_exceptions COMMAND parser_exceptions)
add_executable(parser_comprehensions compiler/parser/comprehensions.cpp)
add_executable(parser_attributes compiler/parser/attributes.cpp)
add_executable(parser_types compiler/parser/types.cpp)
add_executable(parser_specifications compiler/parser/specifications.cpp)
clause_project_options(parser_specifications)
target_include_directories(parser_specifications PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_specifications PRIVATE clause_frontend)
add_test(NAME parser_specifications COMMAND parser_specifications)
clause_project_options(parser_types)
target_include_directories(parser_types PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_types PRIVATE clause_frontend)
add_test(NAME parser_types COMMAND parser_types)
clause_project_options(parser_attributes)
target_include_directories(parser_attributes PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_attributes PRIVATE clause_frontend)
add_test(NAME parser_attributes COMMAND parser_attributes)
clause_project_options(parser_comprehensions)
target_include_directories(parser_comprehensions PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_comprehensions PRIVATE clause_frontend)
add_test(NAME parser_comprehensions COMMAND parser_comprehensions)
set_tests_properties(parser_comprehensions PROPERTIES TIMEOUT 20)
set_tests_properties(parser_exceptions PROPERTIES TIMEOUT 20)
set_tests_properties(parser_control PROPERTIES TIMEOUT 20)
foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    add_test(NAME parser_phase4_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DPHASE=phase4" "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DDUMP=$<TARGET_FILE:parser_dump>" "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
        "-DLIVE=${live}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/phase2.cmake")
    add_test(NAME parser_phase5_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DPHASE=phase5" "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DDUMP=$<TARGET_FILE:parser_dump>" "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
        "-DLIVE=${live}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/phase2.cmake")
endforeach()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(parser_phase4_oracle PROPERTIES TIMEOUT 180)
endif()

if(CLAUSE_OTP_AUDITS)
add_test(NAME otp_oracle COMMAND "${CMAKE_COMMAND}"
    "-DESCRIPT=${CLAUSE_ESCRIPT}"
    "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/oracle.escript"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/preprocessor"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/oracle"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/oracle.cmake")
endif()

add_executable(lexer_dump compiler/lexer_dump.cpp)
clause_project_options(lexer_dump)
target_link_libraries(lexer_dump PRIVATE clause_frontend)
if(CLAUSE_OTP_AUDITS)
add_test(NAME scanner_oracle COMMAND "${CMAKE_COMMAND}"
    "-DESCRIPT=${CLAUSE_ESCRIPT}"
    "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/scan.escript"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/preprocessor"
    "-DDUMP=$<TARGET_FILE:lexer_dump>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/scan.cmake")
endif()
add_test(NAME lexer_golden COMMAND "${CMAKE_COMMAND}"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/preprocessor"
    "-DDUMP=$<TARGET_FILE:lexer_dump>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/golden.cmake")

add_executable(preprocessor_forms compiler/preprocessor/forms.cpp)
clause_project_options(preprocessor_forms)
target_link_libraries(preprocessor_forms PRIVATE clause_frontend)
add_test(NAME preprocessor_forms COMMAND preprocessor_forms)
set_tests_properties(preprocessor_forms PROPERTIES TIMEOUT 10)

add_executable(preprocessor_semantics compiler/preprocessor/semantics.cpp)
clause_project_options(preprocessor_semantics)
target_link_libraries(preprocessor_semantics PRIVATE clause_frontend)
add_test(NAME preprocessor_semantics COMMAND preprocessor_semantics)
set_tests_properties(preprocessor_semantics PROPERTIES TIMEOUT 20)

add_executable(preprocessor_dump compiler/preprocessor/dump.cpp)
clause_project_options(preprocessor_dump)
target_link_libraries(preprocessor_dump PRIVATE clause_frontend)
foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    add_test(NAME preprocessor_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DDUMP=$<TARGET_FILE:preprocessor_dump>"
        "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/preprocessor"
        "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/epp_scan.escript"
        "-DLIVE=${live}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/preprocessor/semantic.cmake")
endforeach()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(preprocessor_oracle PROPERTIES TIMEOUT 60)
endif()

add_test(NAME parser_reference COMMAND "${CMAKE_COMMAND}"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
    "-DDUMP=$<TARGET_FILE:lexer_dump>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/reference.cmake")
if(CLAUSE_OTP_AUDITS)
add_test(NAME parser_oracle COMMAND "${CMAKE_COMMAND}"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
    "-DESCRIPT=${CLAUSE_ESCRIPT}"
    "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.cmake")
endif()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(parser_oracle PROPERTIES TIMEOUT 180)
endif()

add_executable(parser_tokens compiler/parser/tokens.cpp)
clause_project_options(parser_tokens)
target_include_directories(parser_tokens PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_tokens PRIVATE clause_frontend)
add_test(NAME parser_tokens COMMAND parser_tokens)

add_executable(parser_ast compiler/parser/ast.cpp)
clause_project_options(parser_ast)
target_include_directories(parser_ast PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_ast PRIVATE clause_frontend)
add_test(NAME parser_ast COMMAND parser_ast)

add_executable(parser_forms compiler/parser/forms.cpp)
clause_project_options(parser_forms)
target_link_libraries(parser_forms PRIVATE clause_frontend)
add_test(NAME parser_forms COMMAND parser_forms)
set_tests_properties(parser_forms PROPERTIES TIMEOUT 20)

add_executable(parser_dump compiler/parser/dump.cpp)
clause_project_options(parser_dump)
target_link_libraries(parser_dump PRIVATE clause_frontend)
foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    add_test(NAME parser_phase1_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DDUMP=$<TARGET_FILE:parser_dump>"
        "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
        "-DLIVE=${live}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/phase1.cmake")
endforeach()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(parser_phase1_oracle PROPERTIES TIMEOUT 60)
endif()

foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    add_test(NAME parser_phase2_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DDUMP=$<TARGET_FILE:parser_dump>"
        "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
        "-DLIVE=${live}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/phase2.cmake")
endforeach()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(parser_phase2_oracle PROPERTIES TIMEOUT 180)
endif()

add_executable(parser_expressions compiler/parser/expressions.cpp)
clause_project_options(parser_expressions)
target_link_libraries(parser_expressions PRIVATE clause_frontend)
add_test(NAME parser_expressions COMMAND parser_expressions)
set_tests_properties(parser_expressions PROPERTIES TIMEOUT 20)

add_executable(parser_clauses compiler/parser/clauses.cpp)
clause_project_options(parser_clauses)
target_include_directories(parser_clauses PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_clauses PRIVATE clause_frontend)
add_test(NAME parser_clauses COMMAND parser_clauses)
set_tests_properties(parser_clauses PROPERTIES TIMEOUT 20)

foreach(mode IN LISTS oracle_modes)
    if(mode STREQUAL "oracle")
        set(live TRUE)
    else()
        set(live FALSE)
    endif()
    add_test(NAME parser_phase3_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DPHASE=phase3"
        "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser"
        "-DDUMP=$<TARGET_FILE:parser_dump>"
        "-DESCRIPT=${CLAUSE_ESCRIPT}"
        "-DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/oracle.escript"
        "-DLIVE=${live}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compiler/parser/phase2.cmake")
endforeach()
if(CLAUSE_OTP_AUDITS)
set_tests_properties(parser_phase3_oracle PROPERTIES TIMEOUT 180)
endif()

add_executable(parser_structural compiler/parser/structural.cpp)
clause_project_options(parser_structural)
target_include_directories(parser_structural PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_structural PRIVATE clause_frontend)
add_test(NAME parser_structural COMMAND parser_structural)
set_tests_properties(parser_structural PROPERTIES TIMEOUT 20)

add_executable(parser_binaries compiler/parser/binaries.cpp)
clause_project_options(parser_binaries)
target_include_directories(parser_binaries PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(parser_binaries PRIVATE clause_frontend)
add_test(NAME parser_binaries COMMAND parser_binaries)
set_tests_properties(parser_binaries PROPERTIES TIMEOUT 20)

add_subdirectory(compiler/project)

add_subdirectory(compiler/codegen)

add_subdirectory(compiler/programs)

add_subdirectory(compiler/linking)

add_subdirectory(compiler/executables)

add_subdirectory(compiler/printing)

add_subdirectory(compiler/inference)

add_subdirectory(compiler/lint)

# Added for parse transforms.
add_subdirectory(compiler/transforms)

# Added for the SQLite cache backend (plan step 62D5).
add_subdirectory(compiler/cache)

# The erlang library module and its inventory (plan step 41B).
add_subdirectory(compiler/builtins)

# Symbol round trips have no observable emitted-object replacement until lowering exists.
add_executable(semantic_symbols compiler/semantic/symbols.cpp)
clause_project_options(semantic_symbols)
target_include_directories(semantic_symbols PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_symbols PRIVATE clause_semantic)
add_test(NAME semantic_symbols COMMAND semantic_symbols)

add_executable(semantic_types compiler/semantic/types.cpp)
clause_project_options(semantic_types)
target_include_directories(semantic_types PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_types PRIVATE clause_semantic)
add_test(NAME semantic_types COMMAND semantic_types)

add_executable(semantic_declared_types compiler/semantic/declared_types.cpp)
clause_project_options(semantic_declared_types)
target_include_directories(semantic_declared_types PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_declared_types PRIVATE clause_semantic)
add_test(NAME semantic_declared_types COMMAND semantic_declared_types)

add_executable(semantic_inference compiler/semantic/inference.cpp)
clause_project_options(semantic_inference)
target_include_directories(semantic_inference PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_inference PRIVATE clause_semantic)
add_test(NAME semantic_inference COMMAND semantic_inference)

# Identity and candidate rollback invariants remain private until clause dispatch is executable.
add_executable(semantic_bindings compiler/semantic/bindings.cpp)
clause_project_options(semantic_bindings)
target_include_directories(semantic_bindings PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_bindings PRIVATE clause_semantic)
add_test(NAME semantic_bindings COMMAND semantic_bindings)
add_executable(semantic_patterns compiler/semantic/patterns.cpp)
clause_project_options(semantic_patterns)
target_include_directories(semantic_patterns PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(semantic_patterns PRIVATE clause_semantic)
add_test(NAME semantic_patterns COMMAND semantic_patterns)
