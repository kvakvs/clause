{application, ssl, [
    {description, "Erlang/OTP SSL application"},
    {vsn, "11.7.7"},
    {modules, [
        clause_ssl_alert,
        clause_ssl_algorithms,
        clause_ssl_app,
        clause_ssl_certificate,
        clause_ssl_connection,
        clause_ssl_engine,
        clause_ssl_hello,
        clause_ssl_options,
        clause_ssl_pem_cache,
        clause_ssl_secrets,
        clause_ssl_session,
        clause_ssl_session_db,
        ssl,
        ssl_session_cache_api
    ]},
    {registered, [ssl_sup, ssl_manager, ssl_pem_cache, tls_connection_sup]},
    {applications, [crypto, public_key, kernel, stdlib]},
    {env, []},
    {mod, {clause_ssl_app, []}},
    {runtime_dependencies, [
        "stdlib-7.0",
        "public_key-1.21.1",
        "kernel-10.3",
        "erts-16.0",
        "crypto-5.8",
        "inets-5.10.7",
        "runtime_tools-1.15.1"
    ]}
]}.
