# Discover and validate project dependencies, downloading pinned headers on Windows.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake")
add_library(clause_project STATIC diagnostics.cpp loader.cpp)
clause_project_options(clause_project)
target_include_directories(clause_project PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(clause_project PRIVATE clause_project_toml)

target_sources(clause_project PRIVATE schema.cpp decode.cpp)

target_sources(clause_project PRIVATE decode_options.cpp)

target_sources(clause_project PRIVATE paths.cpp)

target_sources(clause_project PRIVATE glob.cpp glob_utf8.cpp)

# The invocation's index of searched directories. Added for the directory index (62E).
target_sources(clause_project PRIVATE discovery.cpp directory_index.cpp)

target_sources(clause_project PRIVATE identity.cpp sources.cpp)
# ignore_dirs and ignore_modules. Added for ignored modules.
target_sources(clause_project PRIVATE ignore.cpp)

target_sources(clause_project PRIVATE selection.cpp)

target_sources(clause_project PRIVATE options.cpp)
target_link_libraries(clause_project PUBLIC clause_frontend)

target_sources(clause_project PRIVATE entry.cpp plan.cpp)

target_sources(clause_project PRIVATE execution.cpp)

target_sources(clause_project PRIVATE cli.cpp command.cpp)

target_sources(clause_project PRIVATE template.cpp)

target_sources(clause_project PRIVATE create.cpp)
