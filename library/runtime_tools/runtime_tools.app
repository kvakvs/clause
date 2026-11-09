{application, runtime_tools, [
    {description, "RUNTIME_TOOLS"},
    {vsn, "2.5"},
    {modules, []},
    {registered, [runtime_tools_sup]},
    {applications, [kernel, stdlib]},
    {env, []},
    {mod, {clause_runtime_tools_app, []}},
    {runtime_dependencies, ["stdlib-6.0", "mnesia-4.12", "kernel-10.0", "erts-16.0"]}
]}.
