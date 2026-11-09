{application, mimapp, [
    {description, "Depends on the applications MongooseIM lists"},
    {vsn, "1.0.0"},
    {modules, []},
    {registered, []},
    {applications, [
        kernel, stdlib, sasl, inets, tools, runtime_tools, asn1, eldap, xmerl, compiler, syntax_tools
    ]},
    {env, []}
]}.
