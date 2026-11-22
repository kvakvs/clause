# Exercise SDK selection/failure in fresh child configurations, reusing the selected SDK.
include("${HOST_SETTINGS}")
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/source" "${TEST_DIR}/private-sdk/lib/cmake/llvm")
file(WRITE "${TEST_DIR}/private-sdk/lib/cmake/llvm/LLVMConfig.cmake"
    "message(FATAL_ERROR \"PRIVATE_PACKAGE_EXECUTED\")\n")
file(WRITE "${TEST_DIR}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(LLVMDependencyProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include("${SOURCE_ROOT}/cmake/LLVMPolicy.cmake")
if(CASE STREQUAL "version")
    clause_check_llvm_version("${TEST_VERSION}")
    return()
endif()
if(CASE STREQUAL "absent" OR CASE STREQUAL "private_only")
    clause_llvm_roots(CMAKE_IGNORE_PREFIX_PATH)
    clause_llvm_search_paths(CMAKE_IGNORE_PATH)
    set(CLAUSE_DOWNLOAD_LLVM OFF CACHE BOOL "")
endif()
include("${SOURCE_ROOT}/cmake/LLVMDependencies.cmake")
add_executable(smoke "${SOURCE_ROOT}/cmake/probes/llvm.cpp")
target_link_libraries(smoke PRIVATE clause_llvm_sdk)
set_target_properties(smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
]=])

# Preserve child diagnostics and check the expected reason, rather than only its exit code.
function(configure_case name expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=LLVM_DIR "CXXFLAGS="
        "${CMAKE_COMMAND}" -S "${TEST_DIR}/source" -B "${TEST_DIR}/${name}"
        ${host_configure_args}
        "-DSOURCE_ROOT=${SOURCE_ROOT}" "-DCASE=${name}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${TEST_DIR}/${name}.log" "${output}\n${error}")
    if(expected STREQUAL "success")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${name}: ${output}\n${error}")
        endif()
    elseif(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected}")
        message(FATAL_ERROR "${name}: expected ${expected}, got ${result}: ${output}\n${error}")
    endif()
    if("${output}${error}" MATCHES "PRIVATE_PACKAGE_EXECUTED")
        message(FATAL_ERROR "Private SDK package was executed")
    endif()
endfunction()

# Exercise explicit selection through a second canonical path to the installed SDK.
configure_case(automatic success)
file(REAL_PATH "${SDK_DIR}" canonical_sdk)
configure_case(selected success "-DLLVM_DIR=${canonical_sdk}")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/selected" --config "${HOST_CONFIG}"
    RESULT_VARIABLE built OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT built EQUAL 0)
    message(FATAL_ERROR "Selected SDK smoke build failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${TEST_DIR}/selected/bin/${HOST_CONFIG}/smoke${HOST_SUFFIX}"
    RESULT_VARIABLE ran OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT ran STREQUAL "0" OR NOT output STREQUAL "" OR NOT error STREQUAL "")
    message(FATAL_ERROR "Selected SDK consumer failed: ${ran}: ${output}${error}")
endif()
configure_case(absent "No global LLVM.*SDK found.*CLAUSE_DOWNLOAD_LLVM is OFF")
configure_case(private "LLVM build-tree SDK is not supported" "-DLLVM_DIR=${TEST_DIR}/private-sdk/lib/cmake/llvm")
configure_case(private_only "No global LLVM.*SDK found.*CLAUSE_DOWNLOAD_LLVM is OFF"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/private-sdk")
configure_case(missing "directory does not exist" "-DLLVM_DIR=${TEST_DIR}/nonexistent-sdk")
foreach(version IN ITEMS 23.1.0 22.1.1 24.1.1 23.1.1git)
    configure_case("version_${version}" "stable.*is required"
        "-DCASE=version" "-DTEST_VERSION=${version}")
endforeach()
configure_case(version_success success "-DCASE=version" "-DTEST_VERSION=23.1.2")

execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CXXFLAGS=" "${CMAKE_COMMAND}"
    -S "${SOURCE_ROOT}" -B "${TEST_DIR}/runtime" ${host_configure_args}
    -DCLAUSE_BUILD_COMPILER=OFF
    -DCLAUSE_BUILD_RUNTIME=ON -DBUILD_TESTING=OFF
    "-DLLVM_DIR=${TEST_DIR}/private-sdk/lib/cmake/llvm" -DCMAKE_DISABLE_FIND_PACKAGE_LLVM=TRUE
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR "${output}${error}" MATCHES "LLVM .*global SDK search")
    message(FATAL_ERROR "Runtime-only configuration depended on LLVM: ${output}\n${error}")
endif()
# The configuration alone shows the independence from LLVM: building it (minutes of the whole runtime) is
# runtime_generated_link's job, the one test that rebuilds the runtime (plan 11 step VAL16).
file(GLOB_RECURSE downloaded "${TEST_DIR}/*-populate*" "${TEST_DIR}/_deps/*")
if(downloaded)
    message(FATAL_ERROR "Dependency discovery created download/bootstrap artifacts: ${downloaded}")
endif()
