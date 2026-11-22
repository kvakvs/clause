add_executable(abi_integers integers.cpp)
clause_project_options(abi_integers)
target_link_libraries(abi_integers PRIVATE clause_abi)
add_test(NAME abi_integers COMMAND abi_integers)
