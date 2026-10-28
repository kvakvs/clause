cmake_minimum_required(VERSION 3.28)
include(cmake/WindowsToolchain.cmake)
project(Clause VERSION 0.1.0 LANGUAGES C CXX)
# Require portable C++23 for every target created in this project and its subdirectories.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules are used; scanning for them would require clang-scan-deps, which distribution Clang may lack.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/WindowsHost.cmake)

option(CLAUSE_BUILD_COMPILER "Build the clau compiler tool" ON)
option(CLAUSE_BUILD_RUNTIME "Build the runtime library" ON)
# Override CTest's default so ordinary builds omit test executables and dependencies.
option(BUILD_TESTING "Build test executables and register CTest tests" OFF)
# Test programs are rebuilt by every run; debug information (PDBs) only helps to debug a crash, so it is
# off unless this is set and the build or test is run again.
option(CLAUSE_TEST_DEBUG_INFO "Build test programs with debug information" OFF)
# Tests drive clau thousands of times; an optimized one halves their compile time. Turn OFF to step through a crash.
option(CLAUSE_OPTIMIZED_COMPILER "Optimize clau and the compiler libraries in Debug builds too" ON)

if(WIN32)
    message(STATUS "Windows development build; remaining platform work is tracked in abi/plan-windows.md.")
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(WARNING "Linux validation is pending. See docs/validation.md for platform status.")
endif()

include(cmake/ProjectOptions.cmake)
if(CLAUSE_BUILD_COMPILER OR CLAUSE_BUILD_RUNTIME)
    include(cmake/BoostDependencies.cmake)
    # Uncompiled headers may use any project target as their IDE analysis context.
    include_directories(SYSTEM ${CLAUSE_BOOST_SYSTEM_INCLUDES})
endif()
if(CLAUSE_BUILD_COMPILER)
    # Select the SDK ABI before creating compiler, runtime or test targets.
    include(cmake/LLVMDependencies.cmake)
endif()
include(CTest)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CLAUSE_MSVC_INCLUDES "$ENV{INCLUDE}")
endif()
configure_file(cmake/QualityToolchain.cmake.in QualityToolchain.cmake @ONLY)
# Keep source quality checks explicit and independent of normal compilation.
# Default targets check changed files and their header dependents; *-all targets check everything.
set(quality_ninja "")
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(quality_ninja "${CMAKE_MAKE_PROGRAM}")
endif()
foreach(scope IN ITEMS changed all)
    set(suffix "")
    if(scope STREQUAL "all")
        set(suffix "-all")
    endif()
    add_custom_target(check-complexity${suffix}
        COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DQUALITY_NINJA=${quality_ninja}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckComplexity.cmake"
        COMMENT "Checking C++ cyclomatic complexity with Lizard (${scope} files)"
        USES_TERMINAL
        VERBATIM
    )
    add_custom_target(check-clang-tidy${suffix}
        COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DQUALITY_NINJA=${quality_ninja}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckClangTidy.cmake"
        COMMENT "Checking C++ diagnostics and cognitive complexity with clang-tidy (${scope} files)"
        USES_TERMINAL
        VERBATIM
    )
    add_custom_target(check-quality${suffix})
    if(CLAUSE_BUILD_COMPILER AND CLAUSE_BUILD_RUNTIME)
        add_dependencies(check-quality${suffix} check-complexity${suffix} check-clang-tidy${suffix})
    else()
        add_custom_command(TARGET check-quality${suffix} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E echo "check-quality${suffix} requires both compiler and runtime enabled."
            COMMAND "${CMAKE_COMMAND}" -E false
            VERBATIM
        )
    endif()
endforeach()
add_subdirectory(abi)
if(CLAUSE_BUILD_COMPILER)
    add_subdirectory(compiler)
endif()
if(CLAUSE_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()
if(BUILD_TESTING AND CLAUSE_BUILD_RUNTIME AND NOT CMAKE_CROSSCOMPILING)
    # Nested test programs link this build's runtime through these targets instead of compiling it again;
    # runtime_link alone keeps proving the standalone, LLVM-free runtime build.
    export(TARGETS clause_generated_program clause_runtime clause_abi clause_multiprecision_dependency
        NAMESPACE Clause:: FILE "${PROJECT_BINARY_DIR}/ClauseRuntimeTargets.cmake")
endif()
if(BUILD_TESTING)
    clause_test_debug_info()
endif()
if(BUILD_TESTING AND CLAUSE_BUILD_COMPILER AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests)
endif()
if(BUILD_TESTING AND CLAUSE_BUILD_RUNTIME AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests/runtime)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests/abi)
endif()
# Each test reserves two CTest processor slots, so `-j N` (presets and wrappers pass every logical CPU)
# runs at most N/2 tests at once; many tests start compilers and nested native builds of their own.
function(clause_reserve_test_processors directory)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    if(tests)
        set_tests_properties(${tests} DIRECTORY "${directory}" PROPERTIES PROCESSORS 2)
    endif()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        clause_reserve_test_processors("${child}")
    endforeach()
endfunction()
# Tests that time out or lose timing races only under a loaded parallel run (saved suite logs 2026-10-12 to
# 2026-10-28). Each waits for every other selected test, so they run last with the processors to themselves, and
# reserves twice the usual slots, so half as many of them run at once. Added for load-sensitive tests.
set(CLAUSE_LOAD_SENSITIVE_TESTS frontend_cli codegen_examples linking_debugger executables_applications
    executables_ets_portions executables_gen_statems executables_interpreted_modules executables_many_ports
    executables_supervisors)
set(CLAUSE_LOAD_SENSITIVE_PROCESSORS 4)

# Collects the test names of a directory and its subdirectories into names_var.
function(clause_collect_tests directory names_var)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    set(names ${${names_var}} ${tests})
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        clause_collect_tests("${child}" names)
    endforeach()
    set(${names_var} "${names}" PARENT_SCOPE)
endfunction()

# Makes each load-sensitive test of a directory tree depend on the others, which CTest honors as start order only,
# and widens its processor reservation.
function(clause_defer_load_sensitive_tests directory others)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    foreach(test IN LISTS tests)
        if(test IN_LIST CLAUSE_LOAD_SENSITIVE_TESTS)
            set_tests_properties(${test} DIRECTORY "${directory}"
                PROPERTIES DEPENDS "${others}" PROCESSORS ${CLAUSE_LOAD_SENSITIVE_PROCESSORS})
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        clause_defer_load_sensitive_tests("${child}" "${others}")
    endforeach()
endfunction()

if(BUILD_TESTING)
    clause_reserve_test_processors("${PROJECT_SOURCE_DIR}")
    set(all_tests "")
    clause_collect_tests("${PROJECT_SOURCE_DIR}" all_tests)
    list(REMOVE_ITEM all_tests ${CLAUSE_LOAD_SENSITIVE_TESTS})
    clause_defer_load_sensitive_tests("${PROJECT_SOURCE_DIR}" "${all_tests}")
endif()
# Compiler dependency discovery may enable C; capture the complete host toolchain for child builds.
configure_file(cmake/TestHost.cmake.in TestHost.cmake @ONLY)
