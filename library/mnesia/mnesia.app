{application, mnesia, [
    {description, "MNESIA  CXC 138 12"},
    {vsn, "4.27"},
    {modules, [
        clause_mnesia_app,
        clause_mnesia_index,
        clause_mnesia_monitor,
        clause_mnesia_schema,
        mnesia,
        mnesia_frag_hash
    ]},
    {registered, [mnesia_sup, mnesia_event, mnesia_ext_sup, mnesia_kernel_sup, mnesia_monitor]},
    {applications, [kernel, stdlib]},
    {env, []},
    {mod, {clause_mnesia_app, []}},
    {runtime_dependencies, ["stdlib-5.0", "kernel-5.3", "erts-9.0"]}
]}.
