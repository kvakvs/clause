{application, inets, [
    {description, "INETS  CXC 138 49"},
    {vsn, "9.8"},
    {modules, []},
    {registered, [inets_sup, httpc_manager]},
    {applications, [kernel, stdlib]},
    {env, []},
    {mod, {clause_inets_app, []}},
    {runtime_dependencies, [
        "stdlib-6.0",
        "stdlib-5.0",
        "ssl-9.0",
        "runtime_tools-1.8.14",
        "mnesia-4.12",
        "kernel-9.0",
        "erts-14.0",
        "public_key-1.13"
    ]}
]}.
