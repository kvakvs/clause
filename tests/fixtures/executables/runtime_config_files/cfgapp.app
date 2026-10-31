{application, cfgapp, [
    {description, "Configured application"},
    {vsn, "1.0"},
    {modules, []},
    {registered, []},
    {applications, [kernel, stdlib]},
    {env, [{a, from_app}, {d, from_app}]}
]}.
