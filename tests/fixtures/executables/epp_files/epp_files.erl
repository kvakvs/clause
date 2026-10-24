%% epp over files this program writes (plan 21 step LIB34): macros with and without arguments, overloading, ??Arg,
%% ?FUNCTION_NAME, ?LINE, ?MODULE, includes and include_lib through the include path, conditionals with -if/-elif,
%% -file, -error/-warning, doc files, the macros option, encodings, and the errors of each directive.
-module(epp_files).
-export([main/1]).

main(_) ->
    [ok = file:make_dir(D) || D <- ["inc", "inc/app", "inc/app/include"]],
    [ok = file:write_file(Name, Content) || {Name, Content} <- files()],
    [
        show(Name, epp:parse_file(Name, [{includes, ["inc"]}]))
     || {Name, _} <- files(), filename:extension(Name) =:= ".erl"
    ],
    show(
        "macros option",
        epp:parse_file("macros.erl", [{includes, ["inc"]}, {macros, ['DEBUG', {'LEVEL', 3}]}])
    ),
    show("bad macros", epp:parse_file("macros.erl", [{macros, ['LINE']}])),
    show("redefine", epp:parse_file("macros.erl", [{macros, [{'LINE', 1, redefine}]}])),
    show("location", epp:parse_file("ifs.erl", [{location, {1, 1}}])),
    show("scan", epp:scan_file("small.erl", [])),
    show("extra", epp:parse_file("small.erl", [extra])),
    show("missing", epp:parse_file("nope.erl", [])),
    show("source name", epp:parse_file("small.erl", [{source_name, "renamed.erl"}])),
    show("deterministic", epp:parse_file("incs.erl", [{includes, ["inc"]}, {deterministic, true}])),
    {ok, Epp} = epp:open("small.erl", []),
    show("forms", [
        epp:parse_erl_form(Epp),
        epp:scan_erl_form(Epp),
        epp:parse_erl_form(Epp),
        epp:parse_erl_form(Epp)
    ]),
    Machine = list_to_atom(erlang:system_info(machine)),
    show(
        "macro defs",
        lists:sort([K || {{atom, K} = Key, _} <- epp:macro_defs(Epp), Key =/= {atom, Machine}])
    ),
    show("close", epp:close(Epp)),
    show("encodings", [epp:read_encoding(F) || F <- ["latin1.erl", "small.erl", "nope.erl"]]),
    show("from binary", [
        epp:read_encoding_from_binary(B)
     || B <- [<<"%% coding: utf-8\n">>, <<"x.\n">>]
    ]),
    show("strings", [epp:encoding_to_string(E) || E <- [latin1, utf8]] ++ [epp:default_encoding()]),
    Errors = [
        cannot_parse,
        {bad, define},
        {duplicated_argument, 'X'},
        missing_parenthesis,
        missing_comma,
        premature_end,
        {call, "?1"},
        {undefined, 'M', none},
        {undefined, 'M', 2},
        {depth, "include"},
        {mismatch, 'M'},
        {arg_error, 'M'},
        {redefine, 'M'},
        {redefine_predef, 'LINE'},
        {circular, 'M', none},
        {circular, 'M', 1},
        {include, file, "x.hrl"},
        {doc, invalid, file},
        {moduledoc, file, "d.md"},
        {illegal, "unbalanced", endif},
        {illegal_function, 'FUNCTION_NAME'},
        {illegal_function_usage, 'FUNCTION_NAME'},
        elif_after_else,
        {'NYI', x},
        {error, "e"},
        {warning, w},
        ftr_after_prefix
    ],
    show("errors", [lists:flatten(epp:format_error(E)) || E <- Errors]),
    io:format("epp_files: ok~n").

show(Label, Result) -> io:format("~ts:~n~p~n", [Label, Result]).

files() ->
    [
        {"inc/defs.hrl", <<"-define(INC, included).\n-record(r, {a}).\n">>},
        {"inc/nested.hrl", <<"-include(\"defs.hrl\").\n-define(NESTED, ?INC).\n">>},
        {"inc/app/include/lib.hrl", <<"-define(LIB, lib).\n">>},
        {"doc.md", <<"Documented text.\n">>},
        {"small.erl", <<"-module(small).\n-export([f/0]).\nf() -> ok.\n">>},
        {"macros.erl", <<
            "-module(macros).\n"
            "-export([f/1]).\n"
            "-define(A, 1).\n"
            "-define(B(X), {X, X}).\n"
            "-define(B(X, Y), [X | Y]).\n"
            "-define(S(E), ??E).\n"
            "-define(F(Fun), Fun(1)).\n"
            "-define(NESTED(X), ?B(?A, X)).\n"
            "-define(SEQ(X), begin X, X end).\n"
            "-ifdef(DEBUG).\n"
            "-define(LOG, debug).\n"
            "-else.\n"
            "-define(LOG, quiet).\n"
            "-endif.\n"
            "f(X) -> {?A, ?B(X), ?B(1, [2]), ?S(X + 1), ?F(fun(Y) -> Y end), ?NESTED(3), ?LOG,\n"
            "         ?MODULE, ?MODULE_STRING, ?FUNCTION_NAME, ?FUNCTION_ARITY, ?LINE, ?SEQ({a, b}),\n"
            "         ?B([1, 2]), ?B(<<1>>), ?B(case X of _ -> 1 end), ?B(fun() -> ok end), ?B(#{a => 1})}.\n"
            "-ifdef(LEVEL).\n"
            "g() -> ?LEVEL.\n"
            "-endif.\n"
        >>},
        {"ifs.erl", <<
            "-module(ifs).\n"
            "-if(?OTP_RELEASE >= 27).\n"
            "a() -> new.\n"
            "-elif(?OTP_RELEASE >= 20).\n"
            "a() -> mid.\n"
            "-else.\n"
            "a() -> old.\n"
            "-endif.\n"
            "-if(defined(NOPE)).\n"
            "b() -> 1.\n"
            "-elif(not defined(NOPE)).\n"
            "b() -> 2.\n"
            "-endif.\n"
            "-ifndef(NOPE).\n"
            "-ifdef(NOPE).\n"
            "c() -> 1.\n"
            "-else.\n"
            "c() -> 2.\n"
            "-endif.\n"
            "-endif.\n"
            "-if(?FEATURE_ENABLED(maybe_expr)).\n"
            "d() -> maybe_on.\n"
            "-endif.\n"
            "-if(1 + foo()).\n"
            "e() -> bad.\n"
            "-endif.\n"
        >>},
        {"incs.erl", <<
            "-module(incs).\n"
            "-include(\"defs.hrl\").\n"
            "-include(\"nested.hrl\").\n"
            "-include_lib(\"app/include/lib.hrl\").\n"
            "-include(\"missing.hrl\").\n"
            "-include_lib(\"missing/include/x.hrl\").\n"
            "f() -> {?INC, ?NESTED, ?LIB, #r{a = 1}, ?FILE}.\n"
        >>},
        {"errors.erl", <<
            "-module(errors).\n"
            "-define(A, 1).\n"
            "-define(A, 2).\n"
            "-define(LINE, 1).\n"
            "-define(B(X, X), X).\n"
            "-define(C(X) X).\n"
            "-define(D, (1).\n"
            "-define(E(X), ?E(X)).\n"
            "-define(F, ?G).\n"
            "-define(G, ?F).\n"
            "f() -> ?UNDEF.\n"
            "g() -> ?A(1).\n"
            "h() -> ?E(1).\n"
            "i() -> ?F.\n"
            "j() -> ?'B'(1).\n"
            "-error(\"stop here\").\n"
            "-warning({careful, 1}).\n"
            "-error(bad.\n"
            "-undef(1).\n"
            "-include(bad).\n"
            "-ifdef(1).\n"
            "-endif.\n"
            "-else.\n"
            "-endif.\n"
            "-elif(true).\n"
            "-file(\"other.erl\", 100).\n"
            "k() -> ?LINE.\n"
            "-file(bad).\n"
            "?FUNCTION_NAME() -> x.\n"
            "-export([?FUNCTION_ARITY]).\n"
            "-feature(maybe_expr, enable).\n"
            "-moduledoc {file, \"doc.md\"}.\n"
            "-doc({file, \"absent.md\"}).\n"
            "-doc {nofile, \"x\"}.\n"
            "-ifdef(OPEN).\n"
        >>},
        {"unterminated.erl", <<"-module(unterminated).\n-ifndef(X).\nf() -> 1.\n">>},
        {"features.erl", <<
            "-module(features).\n"
            "-feature(maybe_expr, disable).\n"
            "-feature(no_such_feature, enable).\n"
            "-if(?FEATURE_ENABLED(maybe_expr)).\n"
            "on() -> yes.\n"
            "-else.\n"
            "on() -> no.\n"
            "-endif.\n"
            "-export([on/0]).\n"
            "-feature(maybe_expr, enable).\n"
        >>},
        {"latin1.erl", <<"%% coding: latin-1\n-module(latin1).\nf() -> \"h", 16#e9, "\".\n">>},
        {"badutf8.erl", <<"-module(badutf8).\nf() -> \"h", 16#e9, "\".\ng() -> 1.\n">>}
    ].
