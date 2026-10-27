{application, syntax_tools, [
    {description, "Syntax tools"},
    {vsn, "4.1.1"},
    {modules, [
        clause_epp_dodger,
        clause_prettypr,
        epp_dodger,
        erl_comment_scan,
        erl_prettypr,
        erl_recomment,
        erl_syntax,
        erl_syntax_lib,
        prettypr
    ]},
    {registered, []},
    {applications, [stdlib]},
    {env, []},
    {runtime_dependencies, ["compiler-9.0", "erts-16.0", "kernel-10.3", "stdlib-8.0"]}
]}.
