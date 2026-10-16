cmake_minimum_required(VERSION 3.25)
# Analyze project translation units using the configured compiler flags.
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED QUALITY_BUILD_DIR)
    set(QUALITY_BUILD_DIR "${project_root}/build/debug")
endif()
set(database "${QUALITY_BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${database}")
    message(FATAL_ERROR "Missing ${database}. Configure a Makefiles or Ninja build first.")
endif()
include("${QUALITY_BUILD_DIR}/QualityToolchain.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/QualityScope.cmake")
set(scoped_sources)
if(QUALITY_SCOPE STREQUAL "changed")
    clause_quality_scope(tidy scoped_sources)
    if(NOT scoped_sources)
        message(STATUS "No changed translation units or header dependents; clang-tidy skipped.")
        clause_quality_record()
        return()
    endif()
endif()
get_filename_component(clang_directory "${quality_clang}" DIRECTORY)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy
    HINTS "${project_root}/.venv-quality/bin" "${project_root}/.venv-quality/Scripts"
        "${clang_directory}"
    REQUIRED
)
set(toolchain_args)
if(quality_frontend STREQUAL "MSVC")
    list(APPEND quality_implicit_includes ${quality_msvc_includes})
endif()
list(REMOVE_DUPLICATES quality_implicit_includes)
# clang-tidy brings its own compiler builtin headers; another Clang release's would not match its builtins.
list(FILTER quality_implicit_includes EXCLUDE REGEX "/lib/clang/[0-9]+/include$")
foreach(directory IN LISTS quality_implicit_includes)
    if(quality_frontend STREQUAL "MSVC")
        list(APPEND toolchain_args "--extra-arg=/imsvc${directory}")
    else()
        list(APPEND toolchain_args "--extra-arg=-isystem${directory}")
    endif()
endforeach()
if(quality_apple_sysroot)
    list(APPEND toolchain_args --extra-arg=-isysroot "--extra-arg=${quality_apple_sysroot}")
endif()

file(READ "${database}" commands)
string(JSON command_count LENGTH "${commands}")
if(command_count EQUAL 0)
    message(FATAL_ERROR "The compilation database is empty.")
endif()
math(EXPR last_command "${command_count} - 1")
# One compilation database entry per selected project source, in database order.
set(sources)
set(entries)
foreach(index RANGE ${last_command})
    string(JSON source GET "${commands}" ${index} file)
    string(JSON directory GET "${commands}" ${index} directory)
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${directory}")
    file(RELATIVE_PATH relative "${project_root}" "${source}")
    if(relative MATCHES "^(compiler|runtime|abi)/" AND NOT source IN_LIST sources
        AND (QUALITY_SCOPE STREQUAL "all" OR relative IN_LIST scoped_sources))
        list(APPEND sources "${source}")
        list(APPEND entries ${index})
    endif()
endforeach()
if(NOT sources)
    message(FATAL_ERROR "No project translation units found in ${database}.")
endif()
list(LENGTH sources source_count)

# QUALITY_SHARD=K/N analyzes only the K-th of N contiguous slices, so one long run can be split into shorter
# separate invocations; without it every selected unit is analyzed.
set(first_unit 0)
set(end_unit ${source_count})
if(DEFINED QUALITY_SHARD AND NOT QUALITY_SHARD STREQUAL "")
    if(NOT QUALITY_SHARD MATCHES "^([1-9][0-9]*)/([1-9][0-9]*)$" OR CMAKE_MATCH_1 GREATER CMAKE_MATCH_2)
        message(FATAL_ERROR "QUALITY_SHARD must be K/N with 1 <= K <= N.")
    endif()
    math(EXPR first_unit "(${CMAKE_MATCH_1} - 1) * ${source_count} / ${CMAKE_MATCH_2}")
    math(EXPR end_unit "${CMAKE_MATCH_1} * ${source_count} / ${CMAKE_MATCH_2}")
endif()

# Run the same translation units/flags independently with LLVM's bounded parallel runner.
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py
    HINTS "${project_root}/.venv-quality/bin" "${project_root}/.venv-quality/Scripts"
        "${clang_directory}" REQUIRED)
# LLVM installations may provide a Python script instead of an executable launcher.
set(tidy_runner "${RUN_CLANG_TIDY_EXECUTABLE}")
if(WIN32 AND NOT RUN_CLANG_TIDY_EXECUTABLE MATCHES "\\.exe$")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    list(PREPEND tidy_runner "${Python3_EXECUTABLE}")
endif()
cmake_host_system_information(RESULT quality_cpus QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT DEFINED QUALITY_JOBS)
    # Half of the logical cores: quality checks run after the tests, so they have the machine to themselves.
    math(EXPR QUALITY_JOBS "${quality_cpus} / 2")
    if(QUALITY_JOBS LESS 1)
        set(QUALITY_JOBS 1)
    endif()
endif()
if(NOT QUALITY_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "QUALITY_JOBS must be a positive integer.")
endif()
# QUALITY_BATCH units per runner invocation; each batch reports when it finishes, so long runs show progress.
if(NOT DEFINED QUALITY_BATCH)
    math(EXPR QUALITY_BATCH "${QUALITY_JOBS} * 4")
endif()
if(NOT QUALITY_BATCH MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "QUALITY_BATCH must be a positive integer.")
endif()
math(EXPR unit_count "${end_unit} - ${first_unit}")
math(EXPR batch_count "(${unit_count} + ${QUALITY_BATCH} - 1) / ${QUALITY_BATCH}")
math(EXPR shown_first "${first_unit} + 1")
message(STATUS "Analyzing units ${shown_first}-${end_unit} of ${source_count} production translation units, "
    "scope ${QUALITY_SCOPE}, in ${batch_count} batches (${QUALITY_JOBS} concurrent jobs)")
set(selected_database "${QUALITY_BUILD_DIR}/quality")
file(MAKE_DIRECTORY "${selected_database}")
list(TRANSFORM toolchain_args REPLACE "^--extra-arg=" "-extra-arg=")
set(failed_batches)
set(batch 0)
set(start ${first_unit})
while(start LESS end_unit)
    math(EXPR batch "${batch} + 1")
    math(EXPR stop "${start} + ${QUALITY_BATCH}")
    if(stop GREATER end_unit)
        set(stop ${end_unit})
    endif()
    # Preserve each selected command verbatim, including its original working directory.
    set(selected_commands "[")
    set(separator "")
    math(EXPR last_unit "${stop} - 1")
    foreach(unit RANGE ${start} ${last_unit})
        list(GET entries ${unit} index)
        string(JSON entry GET "${commands}" ${index})
        string(APPEND selected_commands "${separator}${entry}")
        set(separator ",")
    endforeach()
    file(WRITE "${selected_database}/compile_commands.json" "${selected_commands}]")
    execute_process(
        COMMAND ${tidy_runner} "-p=${selected_database}" "-j=${QUALITY_JOBS}" -quiet
            "-clang-tidy-binary=${CLANG_TIDY_EXECUTABLE}" "-config-file=${project_root}/.clang-tidy" ${toolchain_args}
        WORKING_DIRECTORY "${project_root}"
        RESULT_VARIABLE runner_result
        OUTPUT_FILE "${selected_database}/output.txt"
        ERROR_FILE "${selected_database}/output.txt"
    )
    # Print the batch's diagnostics without analyzer reports located in Boost; the filter decides the result.
    execute_process(
        COMMAND "${QUALITY_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/tidy_filter.py" "${selected_database}/output.txt"
            "${runner_result}"
        RESULT_VARIABLE tidy_result
    )
    math(EXPR shown_start "${start} + 1")
    if(tidy_result STREQUAL "0")
        message(STATUS "clang-tidy batch ${batch}/${batch_count} passed (units ${shown_start}-${stop})")
    else()
        list(APPEND failed_batches "${batch} (units ${shown_start}-${stop})")
        message(STATUS "clang-tidy batch ${batch}/${batch_count} FAILED (units ${shown_start}-${stop})")
    endif()
    set(start ${stop})
endwhile()
if(failed_batches)
    list(JOIN failed_batches ", " failed_text)
    message(FATAL_ERROR "clang-tidy failed in batches ${failed_text}. Fix the diagnostics above before committing.")
endif()
clause_quality_record()

