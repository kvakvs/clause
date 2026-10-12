# Added for the SQLite cache backend (plan step 62D5): the pinned SQLite amalgamation, a compiler-private
# dependency of the cache library only (the runtime never links it).
set(CLAUSE_SQLITE_ROOT "" CACHE PATH "Directory holding the SQLite 3.54.0 amalgamation (sqlite3.c and sqlite3.h)")
set(CLAUSE_SQLITE_VERSION 3.54.0)
include("${PROJECT_SOURCE_DIR}/cmake/ThirdPartyDependencies.cmake")
if(CLAUSE_SQLITE_ROOT)
    set(sqlite_source "${CLAUSE_SQLITE_ROOT}")
else()
    # The amalgamation is portable source, so every host builds the same pinned version.
    clause_download_dependency(sqlite-amalgamation-3540000
        "https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip"
        "68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d"
        "sqlite3.c" sqlite_source)
endif()
if(NOT EXISTS "${sqlite_source}/sqlite3.c" OR NOT EXISTS "${sqlite_source}/sqlite3.h")
    message(FATAL_ERROR "SQLite ${CLAUSE_SQLITE_VERSION} amalgamation is required. Set CLAUSE_SQLITE_ROOT to the directory holding sqlite3.c and sqlite3.h; see docs/projects.md.")
endif()
file(STRINGS "${sqlite_source}/sqlite3.h" sqlite_version_line REGEX "^#define SQLITE_VERSION +\"")
if(NOT sqlite_version_line MATCHES "\"${CLAUSE_SQLITE_VERSION}\"")
    message(FATAL_ERROR "Expected tested SQLite ${CLAUSE_SQLITE_VERSION} under ${sqlite_source}")
endif()
message(STATUS "SQLite ${CLAUSE_SQLITE_VERSION} amalgamation: ${sqlite_source}")

# Third-party C code: built without the project's warning flags.
add_library(clause_sqlite STATIC "${sqlite_source}/sqlite3.c")
target_include_directories(clause_sqlite SYSTEM INTERFACE "${sqlite_source}")
target_compile_definitions(clause_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_DEPRECATED SQLITE_OMIT_SHARED_CACHE SQLITE_LIKE_DOESNT_MATCH_BLOBS)
if(MSVC)
    target_compile_options(clause_sqlite PRIVATE /W0)
else()
    target_compile_options(clause_sqlite PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(clause_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
