# Shared scope selection for check-complexity and check-clang-tidy scripts.
if(NOT DEFINED QUALITY_SCOPE)
    set(QUALITY_SCOPE all)
endif()
if(NOT QUALITY_SCOPE MATCHES "^(all|changed)$")
    message(FATAL_ERROR "QUALITY_SCOPE must be all or changed, not '${QUALITY_SCOPE}'.")
endif()
if(NOT DEFINED QUALITY_PYTHON)
    if(CMAKE_HOST_WIN32)
        set(QUALITY_PYTHON "${project_root}/.venv-quality/Scripts/python.exe")
    else()
        set(QUALITY_PYTHON "${project_root}/.venv-quality/bin/python")
    endif()
endif()

# Store the changed files of one check kind (tidy or lizard) in output, relative to the project root.
function(clause_quality_scope kind output)
    set(base HEAD)
    if(DEFINED ENV{CLAUSE_QUALITY_BASE})
        set(base "$ENV{CLAUSE_QUALITY_BASE}")
    endif()
    set(list_file "${QUALITY_BUILD_DIR}/quality/${kind}-scope.txt")
    file(MAKE_DIRECTORY "${QUALITY_BUILD_DIR}/quality")
    execute_process(
        COMMAND "${QUALITY_PYTHON}" "${project_root}/cmake/quality_scope.py" --kind ${kind}
            --root "${project_root}" --build "${QUALITY_BUILD_DIR}" "--ninja=${QUALITY_NINJA}"
            --base "${base}" --output "${list_file}"
        RESULT_VARIABLE scope_result
    )
    if(NOT scope_result STREQUAL "0")
        message(FATAL_ERROR "Quality scope selection failed; use the *-all quality targets to check everything.")
    endif()
    file(STRINGS "${list_file}" files)
    set(${output} "${files}" PARENT_SCOPE)
endfunction()

# Record every unit's compile command after a passing clang-tidy run, so a later changed scope adds only units
# whose command differs (a build rule edit no longer selects the whole tree). Partial (sharded) runs record nothing.
function(clause_quality_record)
    if(DEFINED QUALITY_SHARD AND NOT QUALITY_SHARD STREQUAL "")
        return()
    endif()
    file(MAKE_DIRECTORY "${QUALITY_BUILD_DIR}/quality")
    execute_process(
        COMMAND "${QUALITY_PYTHON}" "${project_root}/cmake/quality_scope.py" --kind record
            --root "${project_root}" --build "${QUALITY_BUILD_DIR}"
        RESULT_VARIABLE record_result
    )
    if(NOT record_result STREQUAL "0")
        message(FATAL_ERROR "Recording the analyzed compile commands failed.")
    endif()
endfunction()

# Merge the commands of one passing batch's units (absolute source paths) into the record, so that after a failing
# run the next changed scope analyzes only what did not pass, not the whole selection again.
function(clause_quality_record_passed sources)
    set(passed_file "${QUALITY_BUILD_DIR}/quality/passed-batch.txt")
    set(lines "")
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${project_root}" "${source}")
        string(APPEND lines "${relative}\n")
    endforeach()
    file(WRITE "${passed_file}" "${lines}")
    execute_process(
        COMMAND "${QUALITY_PYTHON}" "${project_root}/cmake/quality_scope.py" --kind record
            --root "${project_root}" --build "${QUALITY_BUILD_DIR}" --passed "${passed_file}"
        RESULT_VARIABLE record_result
    )
    if(NOT record_result STREQUAL "0")
        message(FATAL_ERROR "Recording the analyzed compile commands failed.")
    endif()
endfunction()
