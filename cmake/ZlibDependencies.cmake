# Build the Windows fallback before LLVM's configure-time link probe needs it.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ThirdPartyDependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WindowsDependencyBuild.cmake")
option(CLAUSE_DOWNLOAD_ZLIB "Download and build missing Windows zlib in thirdparty" ON)

# Retain separate installations for each compiler, architecture and CRT combination.
function(clause_build_zlib output)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Install target zlib and set ZLIB_ROOT for cross-compilation.")
    endif()
    clause_download_dependency(zlib-1.3.2
        "https://zlib.net/fossils/zlib-1.3.2.tar.gz"
        "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
        "CMakeLists.txt" source)
    clause_build_windows_dependency(prefix NAME zlib-1.3.2 SOURCE "${source}"
        HEADER zconf.h RELEASE_LIBRARY zs.lib DEBUG_LIBRARY zsd.lib
        OPTIONS -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF)
    set(${output} "${prefix}" PARENT_SCOPE)
endfunction()

# zlib for the runtime's zlib module and compressed external terms (plan step 41Q, docs/compile.md#zlib): the release
# OTP 29 bundles, compiled from its sources into an object library whose objects join `target`, as PCRE2's do, so
# compressed output equals OTP's. CLAUSE_RUNTIME_ZLIB_ROOT selects a source directory; otherwise the pinned release
# is downloaded into thirdparty/ on every host.
set(CLAUSE_RUNTIME_ZLIB_ROOT "" CACHE PATH "zlib 1.3.2 source directory for the runtime; takes precedence over the download")

function(clause_add_runtime_zlib target)
    if(CLAUSE_RUNTIME_ZLIB_ROOT)
        set(source "${CLAUSE_RUNTIME_ZLIB_ROOT}")
    else()
        clause_download_dependency(zlib-1.3.2
            "https://zlib.net/fossils/zlib-1.3.2.tar.gz"
            "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
            "zlib.h" source)
    endif()
    file(STRINGS "${source}/zlib.h" version_line REGEX "^#define ZLIB_VERSION ")
    if(NOT version_line MATCHES "\"1\.3\.2\"")
        message(FATAL_ERROR "Expected zlib 1.3.2 under ${source}")
    endif()
    if(NOT TARGET clause_runtime_zlib)
        # The library sources of the release, without the gz* file functions the runtime never calls.
        set(files)
        foreach(name IN ITEMS adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
            list(APPEND files "${source}/${name}.c")
        endforeach()
        add_library(clause_runtime_zlib OBJECT ${files})
        target_include_directories(clause_runtime_zlib SYSTEM PUBLIC "${source}")
        set_target_properties(clause_runtime_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            target_compile_options(clause_runtime_zlib PRIVATE /W0)
            target_compile_definitions(clause_runtime_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
        else()
            target_compile_options(clause_runtime_zlib PRIVATE -w)
        endif()
        message(STATUS "zlib 1.3.2 for the runtime: ${source}")
    endif()
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:clause_runtime_zlib>)
    target_include_directories(${target} SYSTEM PRIVATE $<TARGET_PROPERTY:clause_runtime_zlib,INTERFACE_INCLUDE_DIRECTORIES>)
endfunction()
