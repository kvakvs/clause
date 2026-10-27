%% Added for parse transforms: runs parse transforms for clau on the host Erlang/OTP (docs/transforms.md#loader).
%% clau writes request.etf into a fresh directory, starts erl, which compiles this module in memory and calls
%% main/1 with [Directory]; the reply goes to reply.etf beside it. Clause's transform host is this module linked with
%% the whole library, its main/1 getting the directory as the program argument (plan 21 LIB77A); it interprets
%% project transforms with clause_interp (LIB77C).
-module(clause_transform_loader).
-export([main/1]).

-define(RELEASE, "29").

%% Run the request and always write a reply, then halt.
main([Directory]) ->
    Reply =
        try
            run(Directory)
        catch
            throw:{reply, Thrown} -> Thrown;
            Class:Reason:Stack -> {failed, text("~tp", [{Class, Reason, Stack}])}
        end,
    ok = file:write_file(filename:join(Directory, "reply.etf"), term_to_binary(Reply)),
    halt(0).

run(Directory) ->
    {ok, Binary} = file:read_file(filename:join(Directory, "request.etf")),
    #{
        forms := Forms,
        options := Options,
        transforms := Transforms,
        file := File,
        sources := Sources,
        paths := Paths
    } = binary_to_term(Binary),
    check_release(File),
    case host() of
        true -> clause_interp:set_path(Paths);
        false -> ok
    end,
    load_sources(Sources),
    transform(Transforms, Forms, Options, File, []).

%% Whether this is Clause's transform host, which interprets project transforms (plan 21 LIB77C); erl compiles them.
host() -> erlang:module_loaded(clause_interp).

%% Transforms compiled by another OTP release may not load or behave; Clause supports OTP 29 only.
check_release(File) ->
    case erlang:system_info(otp_release) of
        ?RELEASE ->
            ok;
        Other ->
            Message =
                "parse transforms need Erlang/OTP " ++ ?RELEASE ++ " on the host, found " ++ Other,
            throw({reply, {error, [{File, none, Message}], []}})
    end.

%% Project transform sources {Path, Forms}, as clau read them (plan 21 LIB77C): interpreted in Clause's transform
%% host, compiled in memory and loaded on erl, before any transform runs.
load_sources(Sources) ->
    lists:foreach(fun load_source/1, Sources).

load_source({Path, Forms}) ->
    case host() of
        true -> loaded_source(Path, clause_interp:load(Forms));
        false -> compile_source(Path, Forms)
    end.

%% Modules of kernel, stdlib and compiler live in sticky directories and cannot be reloaded: a project source of
%% one (such as OTP's own qlc_pt.erl on a search path) is not compiled, the host's module is used.
host_sticky(Module) ->
    _ = code:ensure_loaded(Module),
    code:is_sticky(Module).

compile_source(Path, Forms) ->
    case host_sticky(hd([Module || {attribute, _, module, Module} <- Forms])) of
        true -> ok;
        false -> loaded_source(Path, compile:forms(Forms, [binary, return_errors, return_warnings]))
    end.

loaded_source(Path, {ok, Module, Binary, _Warnings}) ->
    {module, Module} = code:load_binary(Module, Path, Binary),
    ok;
loaded_source(_Path, {ok, _Module, _Warnings}) ->
    ok;
loaded_source(Path, {error, Errors, _Warnings}) ->
    Hint = {
        Path,
        none,
        "cannot compile parse transform source; precompile it into a .beam with the host "
        "Erlang/OTP and pass its directory with --transform-path"
    },
    throw({reply, {error, messages(Errors) ++ [Hint], []}}).

%% Load a transform module: from the code path on erl; in Clause's transform host a linked or interpreted module, or
%% one it interprets from a .beam on the code path (plan 21 LIB77C).
ensure(Transform) ->
    case host() of
        true -> clause_interp:ensure_loaded(Transform);
        false -> code:ensure_loaded(Transform)
    end.

%% compile:foldl_transform/3: each transform gets the previous one's forms; the first error stops the chain.
transform([Transform | Rest], Forms0, Options, File, Warnings) ->
    Loaded = ensure(Transform),
    case Loaded =:= {module, Transform} andalso erlang:function_exported(Transform, parse_transform, 2) of
        false ->
            {error, [{File, none, unavailable(Transform, Loaded)}], Warnings};
        true ->
            Forms = maybe_strip_columns(Forms0, Transform, Options),
            try Transform:parse_transform(Forms, Options) of
                {error, Errors, More} ->
                    {error, messages(Errors), Warnings ++ messages(More)};
                {warning, Next, More} ->
                    transform(Rest, Next, Options, File, Warnings ++ messages(More));
                Next ->
                    transform(Rest, Next, Options, File, Warnings)
            catch
                Class:Reason:Stack ->
                    Error =
                        {File, none, compile_error({parse_transform, Transform, {Class, Reason, Stack}})},
                    {error, [Error], Warnings}
            end
    end;
transform([], Forms, Options, _File, Warnings) ->
    {ok, formatted(strip_columns_if(Forms, option_location(Options) =:= line)), Warnings}.

%% Error and warning forms the transforms return, formatted here with Module:format_error/1 since clau cannot
%% (plan 21 LIB77D); Clause's own source problems (clause_source) stay as they came.
formatted(Forms) ->
    [formatted_form(Form) || Form <- Forms].

formatted_form({Kind, {Location, Module, Descriptor}}) when
    (Kind =:= error orelse Kind =:= warning), is_atom(Module), Module =/= clause_source, Module =/= clause_message
->
    {Kind, {Location, clause_message, format(Module, Descriptor)}};
formatted_form(Form) ->
    Form.

%% Why a transform cannot run: a .beam on the code path that does not load is named, one without debug
%% information (which Clause's host needs, plan 21 LIB77C) asks for erl, else OTP's undefined text.
unavailable(Transform, {error, {no_debug_info, Path}}) ->
    text("parse transform '~ts' in ~ts has no debug_info; pass --erl to run it on the host Erlang/OTP", [
        Transform, Path
    ]);
unavailable(Transform, {error, {Reason, Path}}) ->
    text("cannot load parse transform '~ts' from ~ts: ~tp", [Transform, Path, Reason]);
unavailable(Transform, Loaded) ->
    case {Loaded, code:where_is_file(atom_to_list(Transform) ++ ".beam")} of
        {{error, Reason}, Path} when is_list(Path) ->
            text("cannot load parse transform '~ts' from ~ts: ~tp", [Transform, Path, Reason]);
        _ ->
            compile_error({undef_parse_transform, Transform})
    end.

%% compile:maybe_strip_columns/3: columns go when the transform or the options ask for line locations.
maybe_strip_columns(Forms, Transform, Options) ->
    Wanted =
        case erlang:function_exported(Transform, parse_transform_info, 0) of
            true -> maps:get(error_location, Transform:parse_transform_info(), column);
            false -> column
        end,
    strip_columns_if(Forms, Wanted =:= line orelse option_location(Options) =:= line).

option_location(Options) ->
    proplists:get_value(error_location, Options, column).

strip_columns_if(Forms, false) ->
    Forms;
strip_columns_if(Forms, true) ->
    Strip = fun(Anno) -> erl_anno:set_location(erl_anno:line(Anno), Anno) end,
    [strip_form(Form, Strip) || Form <- Forms].

strip_form({eof, {Line, _Column}}, _Strip) ->
    {eof, Line};
strip_form({Kind, {{Line, _Column}, Module, Reason}}, _Strip) when
    Kind =:= error; Kind =:= warning
->
    {Kind, {Line, Module, Reason}};
strip_form(Form, Strip) ->
    erl_parse:map_anno(Strip, Form).

%% [{File, [{Location, Module, Descriptor}]}] as flat [{File, Location, Text}].
messages(Groups) ->
    [
        {File, Location, format(Module, Descriptor)}
     || {File, Items} <- Groups, {Location, Module, Descriptor} <- Items
    ].

format(Module, Descriptor) ->
    try
        text("~ts", [Module:format_error(Descriptor)])
    catch
        _:_ -> text("~tp", [Descriptor])
    end.

compile_error(Descriptor) ->
    text("~ts", [compile:format_error(Descriptor)]).

text(Format, Arguments) ->
    unicode:characters_to_list(io_lib:format(Format, Arguments)).
