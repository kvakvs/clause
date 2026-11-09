{application, tools, [
    {description, "DEVTOOLS  CXC 138 16"},
    {vsn, "4.2.3"},
    {modules, []},
    {registered, []},
    {applications, [kernel, stdlib]},
    {env, [{file_util_search_methods, [{"", ""}, {"ebin", "esrc"}, {"ebin", "src"}]}]},
    {runtime_dependencies, [
        "stdlib-6.0", "runtime_tools-2.1", "kernel-10.0", "erts-15.0", "compiler-8.5", "public_key-1.21", "crypto-5.9"
    ]}
]}.
