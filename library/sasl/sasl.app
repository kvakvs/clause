{application, sasl, [
    {description, "SASL  CXC 138 11"},
    {vsn, "4.4"},
    {modules, []},
    {registered, [sasl_sup, alarm_handler, release_handler]},
    {applications, [kernel, stdlib]},
    {env, []},
    {mod, {clause_sasl_app, []}},
    {runtime_dependencies, ["tools-2.6.14", "stdlib-4.0", "kernel-6.0", "erts-15.0"]}
]}.
