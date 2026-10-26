{application, crypto, [
    {description, "CRYPTO"},
    {vsn, "5.10"},
    {modules, [crypto]},
    {registered, []},
    {applications, [kernel, stdlib]},
    {env, [{fips_mode, false}, {rand_cache_size, 896}]},
    {runtime_dependencies, ["erts-9.0", "stdlib-3.9", "kernel-6.0"]}
]}.
