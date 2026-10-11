# Executable entry selection, startup and linking workflows (F01) through the public CLI.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME linking_entry COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/linking/entry"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/entry/$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/entry.cmake")
set_tests_properties(linking_entry PROPERTIES TIMEOUT 120)
add_test(NAME linking_escript COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:clau>" "-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/linking"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/escript/$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/escript.cmake")
set_tests_properties(linking_escript PROPERTIES TIMEOUT 120)
# -g line tables through includes and macros, as IR metadata and object sections of every target format (F30).
add_test(NAME linking_debug_info COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/debug_info.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/linking/debug"
    "${CMAKE_CURRENT_BINARY_DIR}/debug_info/$<CONFIG>")
set_tests_properties(linking_debug_info PROPERTIES TIMEOUT 300)
# Startup objects linked with the runtime by the native harness recipe; exit paths and startup IR.
if(CLAUSE_BUILD_RUNTIME)
    add_test(NAME linking_startup COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:clau>" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/startup/$<CONFIG>" "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
        "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/startup.cmake")
    set_tests_properties(linking_startup PROPERTIES TIMEOUT 600)
    # Executables linked by clau -o; clause_artifacts serves as a same-target archive lacking runtime symbols.
    add_test(NAME linking_executable COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:clau>" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/executable/$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        "-DOTHER_LIBRARY=$<TARGET_FILE:clause_artifacts>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/executable.cmake")
    set_tests_properties(linking_executable PROPERTIES TIMEOUT 300)
    # Project targets linked to their manifest outputs: selection, -o/--entry rules, aliasing, deferred publication.
    add_test(NAME linking_project COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:clau>" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/project/$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/project.cmake")
    set_tests_properties(linking_project PROPERTIES TIMEOUT 300)
    # A scripted LLDB/GDB session on a -g program: Erlang line breakpoints and the Erlang stack; skipped without one.
    add_test(NAME linking_debugger COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/debugger.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/linking/debug"
        "${CMAKE_CURRENT_BINARY_DIR}/debugger/$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(linking_debugger PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
    # --profile on a program with a known hot function: it ranks first; unprofiled runs are unchanged (F31).
    add_test(NAME linking_profiling COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/profiling.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/linking/profile"
        "${CMAKE_CURRENT_BINARY_DIR}/profiling/$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(linking_profiling PROPERTIES TIMEOUT 600)
    # --lto: program fixtures and selected executable goldens linked as bitcode by LLD; sizes recorded (F32).
    add_test(NAME linking_lto COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/lto.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lto/$<CONFIG>"
        "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(linking_lto PROPERTIES TIMEOUT 900)
    # Export liveness: unreachable exports left out, --keep-exports and keep_exports keep them; sizes recorded (62C).
    add_test(NAME linking_exports COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/exports.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/exports/$<CONFIG>"
        "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(linking_exports PROPERTIES TIMEOUT 900)
endif()
