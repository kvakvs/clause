# Parser is compiler-only; Multiprecision is shared with the independently built runtime.
set(CLAUSE_BOOST_PARSER_ROOT "" CACHE PATH "Standalone Boost.Parser source or installed Boost prefix")
include("${CMAKE_CURRENT_LIST_DIR}/BoostDependencies.cmake")

unset(CLAUSE_BOOST_PARSER_INCLUDE CACHE)
if(CLAUSE_BOOST_PARSER_ROOT)
    find_path(CLAUSE_BOOST_PARSER_INCLUDE boost/parser/parser.hpp
        PATHS "${CLAUSE_BOOST_PARSER_ROOT}" "${CLAUSE_BOOST_PARSER_ROOT}/include"
        NO_DEFAULT_PATH)
else()
    find_path(CLAUSE_BOOST_PARSER_INCLUDE boost/parser/parser.hpp
        HINTS "${CLAUSE_BOOST_INCLUDE}" ${boost_hints}
        PATHS "${PROJECT_SOURCE_DIR}/thirdparty/boost-parser/include"
            "${PROJECT_SOURCE_DIR}/thirdparty/boost_1_90_0")
endif()
if(NOT CLAUSE_BOOST_PARSER_INCLUDE)
    message(FATAL_ERROR "Boost.Parser is required. Install Boost 1.90 or newer (brew install boost on macOS), or see docs/building.md.")
endif()
if(EXISTS "${CLAUSE_BOOST_PARSER_INCLUDE}/boost/version.hpp")
    clause_check_boost_version("${CLAUSE_BOOST_PARSER_INCLUDE}")
else()
    file(SHA256 "${CLAUSE_BOOST_PARSER_INCLUDE}/boost/parser/parser.hpp" parser_hash)
    if(NOT parser_hash STREQUAL "f043c986bec69fe029c53921076b71698c8e745730ff4e1d54cc10199a880e0f")
        message(FATAL_ERROR "Standalone Boost.Parser must match the pinned 1.90.0 release; alternatively use an installed Boost 1.90 or newer.")
    endif()
endif()

message(STATUS "Boost.Parser headers: ${CLAUSE_BOOST_PARSER_INCLUDE}")
add_library(clause_parser_dependency INTERFACE)
target_include_directories(clause_parser_dependency SYSTEM INTERFACE
    "${CLAUSE_BOOST_PARSER_INCLUDE}")
target_link_libraries(clause_parser_dependency INTERFACE clause_multiprecision_dependency)
# cl emits C4702 from Boost.Parser templates at code generation, bypassing /external:W0.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(clause_parser_dependency INTERFACE /wd4702)
endif()
