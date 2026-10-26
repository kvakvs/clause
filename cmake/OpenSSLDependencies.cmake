# OpenSSL's libcrypto for the runtime's crypto archive (plan 21 step LIB79, docs/libraries/crypto.md). The crypto
# builtins use only API present since OpenSSL 3.1.2 (the latest FIPS-validated provider release), so any libcrypto
# of at least that version links with them.
# Resolution order: CLAUSE_OPENSSL_ROOT (an installed prefix or a source tree), an installed OpenSSL
# (CLAUSE_OPENSSL_SYSTEM), then the pinned release downloaded into thirdparty/ and built once per architecture and
# CRT. clause_add_openssl(target) gives `target` the headers and libcrypto, and records what clau must add to link an
# executable with the crypto archive (CLAUSE_LINK_ARGUMENTS) and how a source tree is configured
# (CLAUSE_OPENSSL_CONFIGURATION, for clau --with-crypto=SOURCE).
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ThirdPartyDependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WindowsDependencyBuild.cmake")

set(CLAUSE_OPENSSL_VERSION 4.1.0)
set(CLAUSE_OPENSSL_MINIMUM 3.1.2 CACHE INTERNAL "Oldest OpenSSL the crypto archive links with" FORCE)
set(CLAUSE_OPENSSL_ROOT "" CACHE PATH "OpenSSL >= ${CLAUSE_OPENSSL_MINIMUM} installation prefix or source tree; takes precedence over installed and downloaded copies")
set(clause_openssl_system_default ON)
if(WIN32)
    set(clause_openssl_system_default OFF)
endif()
option(CLAUSE_OPENSSL_SYSTEM "Use an installed OpenSSL >= ${CLAUSE_OPENSSL_MINIMUM} when CLAUSE_OPENSSL_ROOT is not set" ${clause_openssl_system_default})
option(CLAUSE_DOWNLOAD_OPENSSL "Download and build OpenSSL ${CLAUSE_OPENSSL_VERSION} into thirdparty when no other copy is found" ON)
# The oldest supported release, whose FIPS provider is the latest validated one: the full test suite links the crypto
# goldens with its libcrypto and loads its FIPS provider into the pinned libcrypto (plan 21 step LIB79F).
set(CLAUSE_OPENSSL_MINIMUM_SHA256 a0ce69b8b97ea6a35b96875235aa453b966ba3cba8af2de23657d8b6767d6539 CACHE INTERNAL "SHA-256 of its source archive" FORCE)
option(CLAUSE_TEST_OPENSSL_MINIMUM "Download OpenSSL ${CLAUSE_OPENSSL_MINIMUM} source for the full crypto tests" ON)

# The source tree of the minimum release in thirdparty/, downloaded once (only extracted: tests build it).
function(clause_openssl_minimum_source output)
    clause_download_dependency(openssl-${CLAUSE_OPENSSL_MINIMUM}
        "https://github.com/openssl/openssl/releases/download/openssl-${CLAUSE_OPENSSL_MINIMUM}/openssl-${CLAUSE_OPENSSL_MINIMUM}.tar.gz"
        "${CLAUSE_OPENSSL_MINIMUM_SHA256}" "Configure" source)
    set(${output} "${source}" PARENT_SCOPE)
endfunction()

# Configure options of every source build: a static libcrypto with the legacy provider built in, as OTP loads it.
set(CLAUSE_OPENSSL_OPTIONS no-shared no-module no-tests no-docs no-makedepend enable-ec_explicit_curves)

# "source" for a source tree, "install" for an installed prefix; fails for anything else.
function(clause_openssl_kind root output)
    if(EXISTS "${root}/Configure" AND EXISTS "${root}/VERSION.dat")
        set(${output} source PARENT_SCOPE)
    elseif(EXISTS "${root}/include/openssl/opensslv.h")
        set(${output} install PARENT_SCOPE)
    else()
        message(FATAL_ERROR "${root} is neither an OpenSSL source tree (Configure, VERSION.dat) nor an installation (include/openssl/opensslv.h)")
    endif()
endfunction()

# The MAJOR.MINOR.PATCH version of a source tree (VERSION.dat) or an installation (opensslv.h).
function(clause_openssl_version root kind output)
    if(kind STREQUAL "source")
        file(STRINGS "${root}/VERSION.dat" lines REGEX "^(MAJOR|MINOR|PATCH)=")
        string(REGEX REPLACE "[A-Z]+=" "" parts "${lines}")
        list(JOIN parts "." version)
    else()
        file(STRINGS "${root}/include/openssl/opensslv.h" line REGEX "define +OPENSSL_VERSION_STR ")
        string(REGEX MATCH "\"([0-9.]+)\"" version "${line}")
        set(version "${CMAKE_MATCH_1}")
    endif()
    if(version VERSION_LESS CLAUSE_OPENSSL_MINIMUM)
        message(FATAL_ERROR "OpenSSL ${version} under ${root} is older than ${CLAUSE_OPENSSL_MINIMUM}")
    endif()
    set(${output} "${version}" PARENT_SCOPE)
endfunction()

# Download a pinned zip into thirdparty/tools/<name> once (archives without a top-level directory, such as
# Strawberry Perl's portable zip, extract into it).
function(clause_download_tool name url sha256 required_file output)
    get_filename_component(tools "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../thirdparty/tools" ABSOLUTE)
    set(directory "${tools}/${name}")
    file(MAKE_DIRECTORY "${tools}")
    file(LOCK "${tools}/.download.lock" GUARD FUNCTION TIMEOUT 1800)
    if(NOT EXISTS "${directory}/.clause-extracted" OR NOT EXISTS "${directory}/${required_file}")
        get_filename_component(archive_name "${url}" NAME)
        set(archive "${tools}/${archive_name}")
        message(STATUS "Downloading ${name} to ${tools}")
        clause_fetch_archive("${name}" "${url}" "${archive}" "${sha256}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${directory}")
        file(REMOVE "${archive}")
        if(NOT EXISTS "${directory}/${required_file}")
            message(FATAL_ERROR "${name} archive is missing ${required_file}")
        endif()
        file(TOUCH "${directory}/.clause-extracted")
    endif()
    set(${output} "${directory}" PARENT_SCOPE)
endfunction()

# The directories holding perl and nasm for a Windows source build: native Strawberry Perl (Git's MSYS perl gives
# Configure Unix paths) and NASM, both pinned.
function(clause_openssl_windows_tools output)
    clause_download_tool(strawberry-perl-5.42.3.1
        "https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54231_64bit/strawberry-perl-5.42.3.1-64bit-portable.zip"
        "6a081a811781c30aca51dbc036afd93092af91e3297901f02c17043795a10690"
        "perl/bin/perl.exe" perl)
    clause_download_tool(nasm-3.02-win64
        "https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip"
        "161d0bfaff53c2f9e9f3e69fd0672323ebabafd1268976a5cec11be92a19aee7"
        "nasm-3.02/nasm.exe" nasm)
    set(${output} "${perl}/perl/bin" "${nasm}/nasm-3.02" PARENT_SCOPE)
endfunction()

# The Configure target and extra arguments of a source build for this toolchain; the target is empty when Configure
# detects it (POSIX hosts).
function(clause_openssl_target target_output arguments_output)
    set(arguments)
    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(target VC-WIN32)
        elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
            set(target VC-WIN64-CLANGASM-ARM)
        else()
            set(target VC-WIN64A)
        endif()
    else()
        set(target "")
        # Static archives join position-independent executables.
        list(APPEND arguments -fPIC)
    endif()
    set(${target_output} "${target}" PARENT_SCOPE)
    set(${arguments_output} "${arguments}" PARENT_SCOPE)
endfunction()

# The configure arguments selecting one MSVC runtime: OpenSSL's static-vcruntime gives /MT, --debug the Debug CRT.
function(clause_openssl_crt_arguments crt output)
    set(arguments)
    if(crt MATCHES "^MultiThreaded(Debug)?$")
        list(APPEND arguments enable-static-vcruntime)
    endif()
    if(crt MATCHES "Debug")
        list(APPEND arguments --debug)
    endif()
    set(${output} "${arguments}" PARENT_SCOPE)
endfunction()

# The Configure options of `options` the source tree knows: enable-X and no-X are kept when X is in one of its
# Configure's `my @disablables... = (...);` lists (older trees lack later options, such as 3.1's no-docs,
# enable-ec_explicit_curves, which it always has, and enable-static-vcruntime, whose /MT its static builds already
# use). Added for the OpenSSL 3.1.2 minimum.
function(clause_openssl_known_options source options output)
    file(READ "${source}/Configure" configure)
    string(REGEX MATCHALL "my @disablables[a-z_]* = (qw)?\\([^;]*\\);" lists "${configure}")
    string(JOIN " " features ${lists})
    string(REGEX REPLACE "#[^\n]*" "" features " ${features} ")
    set(known)
    foreach(option IN LISTS options)
        string(REGEX REPLACE "^(enable-|no-)" "" name "${option}")
        string(REGEX MATCH "[^A-Za-z0-9_-]${name}[^A-Za-z0-9_-]" found "${features}")
        if(name STREQUAL option OR found)
            list(APPEND known "${option}")
        endif()
    endforeach()
    set(${output} "${known}" PARENT_SCOPE)
endfunction()

# Configure, build and install the libraries of one source tree into `prefix`; also writes prefix/ex_libs.txt with
# the system libraries libcrypto needs (the makefile's CNF_EX_LIBS and EX_LIBS).
function(clause_openssl_build source prefix work crt)
    clause_openssl_target(target arguments)
    clause_openssl_crt_arguments("${crt}" crt_arguments)
    clause_openssl_known_options("${source}" "${CLAUSE_OPENSSL_OPTIONS};${crt_arguments}" options)
    set(path "$ENV{PATH}")
    if(WIN32)
        clause_openssl_windows_tools(tools)
        list(GET tools 0 perl_directory)
        set(perl "${perl_directory}/perl.exe")
        string(REPLACE ";" "\\;" path "${path}")
        list(JOIN tools "\\;" tool_path)
        set(path "${tool_path}\\;${path}")
        find_program(CLAUSE_NMAKE nmake REQUIRED)
        set(make "${CLAUSE_NMAKE}" /nologo)
    else()
        find_program(CLAUSE_PERL perl REQUIRED)
        set(perl "${CLAUSE_PERL}")
        find_program(CLAUSE_MAKE NAMES gmake make REQUIRED)
        cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)
        math(EXPR jobs "(${cores} * 2 + 2) / 3")
        set(make "${CLAUSE_MAKE}" -j${jobs})
    endif()
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    set(environment "${CMAKE_COMMAND}" -E env "PATH=${path}" "CC=${CMAKE_C_COMPILER}")
    message(STATUS "Building OpenSSL from ${source} (CRT ${crt}) in ${work}")
    execute_process(COMMAND ${environment} "${perl}" "${source}/Configure" ${target} ${options}
            ${arguments} "--prefix=${prefix}" "--openssldir=${prefix}/ssl" --libdir=lib
        WORKING_DIRECTORY "${work}" OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${environment} ${make} build_libs WORKING_DIRECTORY "${work}"
        OUTPUT_FILE "${work}/build.log" ERROR_FILE "${work}/build.log" COMMAND_ERROR_IS_FATAL ANY)
    # OpenSSL 3.x's Windows install_dev of a static build copies ossl_static.pdb, which clang-cl's /Z7 never writes.
    if(WIN32 AND NOT EXISTS "${work}/ossl_static.pdb")
        file(TOUCH "${work}/ossl_static.pdb")
    endif()
    execute_process(COMMAND ${environment} ${make} install_dev WORKING_DIRECTORY "${work}"
        OUTPUT_FILE "${work}/install.log" COMMAND_ERROR_IS_FATAL ANY)
    file(STRINGS "${work}/makefile" lines REGEX "^(CNF_)?EX_LIBS *=")
    string(REGEX REPLACE "(CNF_)?EX_LIBS *= *" "" libraries "${lines}")
    string(REGEX REPLACE "[ ;]+" ";" libraries "${libraries}")
    list(REMOVE_ITEM libraries "")
    list(JOIN libraries "\n" text)
    file(WRITE "${prefix}/ex_libs.txt" "${text}\n")
    file(REMOVE_RECURSE "${work}")
endfunction()

# The CRT families a Windows build needs OpenSSL for: one, or Release and Debug for a per-configuration selection.
function(clause_openssl_crts output)
    set(crt "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    if(CLAUSE_DOWNLOADED_LLVM AND CLAUSE_DEFAULT_MSVC_RUNTIME)
        set(crt MultiThreaded)
    endif()
    if(NOT WIN32)
        set(crt none)
    endif()
    string(REPLACE "$<$<CONFIG:Debug>:Debug>" "" family "${crt}")
    if(family STREQUAL "")
        set(family MultiThreadedDLL)
    endif()
    if(family STREQUAL crt)
        set(${output} "${crt}" PARENT_SCOPE)
    else()
        string(REPLACE "MultiThreaded" "MultiThreadedDebug" debug "${family}")
        set(${output} "${family}" "${debug}" PARENT_SCOPE)
    endif()
endfunction()

# Build `source` once per CRT into thirdparty/openssl-<version>-<arch>-<crt>/install, rebuilding when the toolchain
# or options change; sets the install prefixes, in clause_openssl_crts order.
function(clause_openssl_from_source source version output)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "OpenSSL source builds need a native toolchain; set CLAUSE_OPENSSL_ROOT to the target's installed OpenSSL.")
    endif()
    string(TOLOWER "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" architecture)
    if(architecture STREQUAL "")
        math(EXPR architecture "${CMAKE_SIZEOF_VOID_P} * 8")
    endif()
    clause_openssl_crts(crts)
    set(prefixes)
    foreach(crt IN LISTS crts)
        unset(previous)
        clause_msvc_runtime_suffix(suffix "${crt}")
        if(NOT WIN32)
            set(suffix "${CMAKE_SYSTEM_NAME}")
        endif()
        get_filename_component(base "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../thirdparty/openssl-${version}-${architecture}-${suffix}" ABSOLUTE)
        file(MAKE_DIRECTORY "${base}")
        file(LOCK "${base}/.build.lock" GUARD FUNCTION TIMEOUT 3600)
        string(JOIN "\n" identity "${source}" "${CLAUSE_OPENSSL_OPTIONS}" "${CMAKE_C_COMPILER}"
            "${CMAKE_C_COMPILER_VERSION}" "${crt}")
        set(stamp "${base}/install/.complete")
        if(EXISTS "${stamp}")
            file(READ "${stamp}" previous)
        endif()
        if(NOT previous STREQUAL identity)
            file(REMOVE_RECURSE "${base}/install")
            clause_openssl_build("${source}" "${base}/install" "${base}/build" "${crt}")
            file(WRITE "${stamp}" "${identity}")
        endif()
        list(APPEND prefixes "${base}/install")
        file(LOCK "${base}/.build.lock" RELEASE)
    endforeach()
    set(${output} "${prefixes}" PARENT_SCOPE)
endfunction()

# libcrypto of an install prefix (also in a multiarch lib/<architecture>): the static archive when there is one,
# else the import or shared library.
function(clause_openssl_library prefix output)
    set(directories lib lib64)
    if(CMAKE_LIBRARY_ARCHITECTURE)
        list(APPEND directories "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    endif()
    foreach(name IN ITEMS libcrypto.lib libcrypto_static.lib libcrypto.a crypto.lib libcrypto.dylib libcrypto.so)
        foreach(directory IN LISTS directories)
            if(EXISTS "${prefix}/${directory}/${name}")
                set(${output} "${prefix}/${directory}/${name}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    message(FATAL_ERROR "No libcrypto under ${prefix}/lib")
endfunction()

# The system libraries a static libcrypto needs: recorded by a source build, else the Libs.private of the
# libcrypto.pc beside an installed one (Ubuntu's names -lz and -lzstd), else the platform's usual set.
function(clause_openssl_system_libraries prefix library output)
    get_filename_component(directory "${library}" DIRECTORY)
    set(package "${directory}/pkgconfig/libcrypto.pc")
    if(EXISTS "${prefix}/ex_libs.txt")
        file(STRINGS "${prefix}/ex_libs.txt" libraries)
    elseif(EXISTS "${package}" AND NOT WIN32)
        file(STRINGS "${package}" private REGEX "^Libs.private:")
        string(REGEX REPLACE "^Libs.private: *" "" private "${private}")
        separate_arguments(libraries UNIX_COMMAND "${private}")
    elseif(WIN32)
        set(libraries ws2_32.lib gdi32.lib advapi32.lib crypt32.lib user32.lib)
    elseif(APPLE)
        set(libraries)
    else()
        set(libraries -ldl -pthread)
    endif()
    set(${output} "${libraries}" PARENT_SCOPE)
endfunction()

# An installed OpenSSL of at least the minimum version, preferring a static libcrypto: sets clause_openssl_prefix
# in the caller, or leaves it empty.
function(clause_find_installed_openssl)
    set(OPENSSL_USE_STATIC_LIBS ON)
    find_package(OpenSSL ${CLAUSE_OPENSSL_MINIMUM} QUIET COMPONENTS Crypto)
    if(NOT OPENSSL_FOUND)
        set(OPENSSL_USE_STATIC_LIBS OFF)
        find_package(OpenSSL ${CLAUSE_OPENSSL_MINIMUM} QUIET COMPONENTS Crypto)
    endif()
    if(OPENSSL_FOUND)
        get_filename_component(prefix "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
        set(clause_openssl_prefix "${prefix}" PARENT_SCOPE)
        set(clause_openssl_library "${OPENSSL_CRYPTO_LIBRARY}" PARENT_SCOPE)
    endif()
endfunction()

# The install prefixes (one per CRT) of the OpenSSL this build uses, and the source tree it was built from (empty
# for an installed copy).
function(clause_resolve_openssl prefixes_output source_output)
    set(source "")
    if(CLAUSE_OPENSSL_ROOT)
        clause_openssl_kind("${CLAUSE_OPENSSL_ROOT}" kind)
        clause_openssl_version("${CLAUSE_OPENSSL_ROOT}" ${kind} version)
        if(kind STREQUAL "install")
            set(${prefixes_output} "${CLAUSE_OPENSSL_ROOT}" PARENT_SCOPE)
            set(${source_output} "" PARENT_SCOPE)
            return()
        endif()
        set(source "${CLAUSE_OPENSSL_ROOT}")
    elseif(CLAUSE_OPENSSL_SYSTEM)
        clause_find_installed_openssl()
        if(clause_openssl_prefix)
            set(${prefixes_output} "${clause_openssl_prefix}" PARENT_SCOPE)
            set(${source_output} "" PARENT_SCOPE)
            return()
        endif()
    endif()
    if(NOT source)
        if(NOT CLAUSE_DOWNLOAD_OPENSSL)
            message(FATAL_ERROR "OpenSSL >= ${CLAUSE_OPENSSL_MINIMUM} is required for the crypto runtime: set CLAUSE_OPENSSL_ROOT, install it, enable CLAUSE_DOWNLOAD_OPENSSL or disable CLAUSE_RUNTIME_CRYPTO (docs/libraries/crypto.md).")
        endif()
        clause_download_dependency(openssl-${CLAUSE_OPENSSL_VERSION}
            "https://github.com/openssl/openssl/releases/download/openssl-${CLAUSE_OPENSSL_VERSION}/openssl-${CLAUSE_OPENSSL_VERSION}.tar.gz"
            "50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd"
            "VERSION.dat" source)
        set(version ${CLAUSE_OPENSSL_VERSION})
    endif()
    clause_openssl_from_source("${source}" "${version}" prefixes)
    set(${prefixes_output} "${prefixes}" PARENT_SCOPE)
    set(${source_output} "${source}" PARENT_SCOPE)
endfunction()

# Give `target` OpenSSL's headers and libcrypto. With two prefixes (Release and Debug CRTs) the Debug configuration
# takes the second. Records the link inputs and the source-build recipe on the target.
function(clause_add_openssl target)
    clause_resolve_openssl(prefixes source)
    list(GET prefixes 0 release)
    list(GET prefixes -1 debug)
    clause_openssl_library("${release}" release_library)
    clause_openssl_library("${debug}" debug_library)
    clause_openssl_system_libraries("${release}" "${release_library}" system_libraries)
    set(library "$<IF:$<CONFIG:Debug>,${debug_library},${release_library}>")
    target_include_directories(${target} SYSTEM PUBLIC "$<IF:$<CONFIG:Debug>,${debug}/include,${release}/include>")
    target_link_libraries(${target} PUBLIC "${library}" ${system_libraries})
    # clause_runtime_crypto.link: libcrypto of the configuration, then the system libraries.
    set_target_properties(${target} PROPERTIES CLAUSE_OPENSSL_RELEASE "${release_library}"
        CLAUSE_OPENSSL_DEBUG "${debug_library}")
    set_property(TARGET ${target} APPEND PROPERTY CLAUSE_LINK_ARGUMENTS ${system_libraries})
    # The recipe clau repeats for --with-crypto=SOURCE: one key=value per line, values space-separated.
    clause_openssl_target(configure_target configure_arguments)
    clause_openssl_crts(crts)
    list(GET crts 0 crt)
    clause_openssl_crt_arguments("${crt}" crt_arguments)
    string(JOIN " " options ${CLAUSE_OPENSSL_OPTIONS} ${configure_arguments} ${crt_arguments})
    set(tools "")
    if(WIN32 AND source)
        clause_openssl_windows_tools(tool_directories)
        list(JOIN tool_directories "|" tools)
    endif()
    set_property(TARGET ${target} PROPERTY CLAUSE_OPENSSL_CONFIGURATION
        "version=${CLAUSE_OPENSSL_VERSION}" "minimum=${CLAUSE_OPENSSL_MINIMUM}" "target=${configure_target}"
        "options=${options}" "cc=${CMAKE_C_COMPILER}" "tools=${tools}" "source=${source}")
    message(STATUS "OpenSSL for the crypto runtime: ${release_library}")
endfunction()
