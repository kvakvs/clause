{application, tapp, [
    {description, "Test application"},
    {vsn, "1.2.3"},
    {modules, [tapp, tapp_sup, tapp_worker]},
    {registered, [tapp_sup, tapp_worker]},
    {applications, [kernel, stdlib]},
    {mod, {tapp, normal_start}},
    {start_phases, [{first, one}, {second, two}]},
    {env, [{color, blue}, {size, 3}]}
]}.
