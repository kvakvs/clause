# Supply the static zstd target required by the official Windows LLVM SDK.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ThirdPartyDependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WindowsDependencyBuild.cmake")
option(CLAUSE_DOWNLOAD_ZSTD "Download and build missing Windows zstd in thirdparty" ON)

# Pin the upstream release archive and retain native Debug/Release installations.
function(clause_build_zstd output)
    clause_download_dependency(zstd-1.5.7
        "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
        "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3"
        "build/cmake/CMakeLists.txt" source)
    clause_build_windows_dependency(prefix NAME zstd-1.5.7 SOURCE "${source}/build/cmake"
        HEADER zstd.h RELEASE_LIBRARY zstd_static.lib DEBUG_LIBRARY zstd_staticd.lib
        OPTIONS -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF
            -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF -DCMAKE_DEBUG_POSTFIX=d)
    set(${output} "${prefix}" PARENT_SCOPE)
endfunction()

# zstd for the runtime's zstd module (plan 21 step LIB54E, docs/builtins.md#zstd-module): the release OTP 29 bundles,
# compiled from its library sources (common, compress, decompress; single-threaded, without the assembly decoder)
# into an object library whose objects join `target`, as zlib's do, so compressed output equals OTP's.
# CLAUSE_RUNTIME_ZSTD_ROOT selects a source directory; otherwise the pinned release is downloaded into thirdparty/.
set(CLAUSE_RUNTIME_ZSTD_ROOT "" CACHE PATH "zstd 1.5.7 source directory for the runtime; takes precedence over the download")

function(clause_add_runtime_zstd target)
    if(CLAUSE_RUNTIME_ZSTD_ROOT)
        set(source "${CLAUSE_RUNTIME_ZSTD_ROOT}")
    else()
        clause_download_dependency(zstd-1.5.7
            "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
            "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3"
            "lib/zstd.h" source)
    endif()
    file(STRINGS "${source}/lib/zstd.h" version_lines REGEX "^#define ZSTD_VERSION_(MAJOR|MINOR|RELEASE) ")
    if(NOT version_lines MATCHES "MAJOR +1;.*MINOR +5;.*RELEASE +7")
        message(FATAL_ERROR "Expected zstd 1.5.7 under ${source}")
    endif()
    if(NOT TARGET clause_runtime_zstd)
        file(GLOB files "${source}/lib/common/*.c" "${source}/lib/compress/*.c" "${source}/lib/decompress/*.c")
        list(SORT files)
        add_library(clause_runtime_zstd OBJECT ${files})
        target_include_directories(clause_runtime_zstd SYSTEM PUBLIC "${source}/lib")
        target_compile_definitions(clause_runtime_zstd PRIVATE ZSTD_DISABLE_ASM=1)
        set_target_properties(clause_runtime_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            target_compile_options(clause_runtime_zstd PRIVATE /W0)
        else()
            target_compile_options(clause_runtime_zstd PRIVATE -w)
        endif()
        message(STATUS "zstd 1.5.7 for the runtime: ${source}")
    endif()
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:clause_runtime_zstd>)
    target_include_directories(${target} SYSTEM PRIVATE $<TARGET_PROPERTY:clause_runtime_zstd,INTERFACE_INCLUDE_DIRECTORIES>)
endfunction()
