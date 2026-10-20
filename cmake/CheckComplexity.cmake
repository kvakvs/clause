# Run the source complexity gate without requiring a configured C++ build.
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED QUALITY_BUILD_DIR)
    set(QUALITY_BUILD_DIR "${project_root}/build/debug")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/QualityScope.cmake")
if(NOT EXISTS "${QUALITY_PYTHON}")
    message(FATAL_ERROR "Install the quality tools as described in docs/building.md, or set QUALITY_PYTHON to the environment's Python executable.")
endif()

if(NOT DEFINED COMPLEXITY_MAX_CCN)
    set(COMPLEXITY_MAX_CCN 10)
endif()
if(NOT COMPLEXITY_MAX_CCN MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "COMPLEXITY_MAX_CCN must be a positive integer.")
endif()

# Limit discovery to project C++ sources; exclude reference and build trees.
set(complexity_inputs compiler runtime abi)
if(QUALITY_SCOPE STREQUAL "changed")
    clause_quality_scope(lizard complexity_inputs)
    if(NOT complexity_inputs)
        message(STATUS "No changed production C++ files; Lizard skipped.")
        return()
    endif()
endif()
execute_process(
    COMMAND "${QUALITY_PYTHON}" -m lizard -l cpp -C "${COMPLEXITY_MAX_CCN}"
        ${complexity_inputs}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE complexity_result
)
if(NOT complexity_result STREQUAL "0")
    message(FATAL_ERROR "Complexity check failed (Lizard exit: ${complexity_result}). See the report above.")
endif()
