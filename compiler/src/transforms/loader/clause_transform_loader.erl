%% Added for parse transforms: runs parse transforms for clau on the host Erlang/OTP (docs/transforms.md#loader).
%% clau writes request.etf into a fresh directory, starts erl, which compiles this module in memory and calls
%% main/1 with [Directory]; the reply goes to reply.etf beside it. Clause's transform host is this module linked with
%% the whole library, its main/1 getting the directory as the program argument (plan 21 LIB77A).
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
        sources := Sources
    } = binary_to_term(Binary),
    check_release(File),
    load_sources(Sources),
    transform(Transforms, Forms, Options, File, []).

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

%% Project transform sources {Path, CompileOptions}: compiled in memory and loaded before any transform runs.
load_sources(Sources) ->
    lists:foreach(fun load_source/1, Sources).

load_source({Path, Options}) ->
    case host_sticky(list_to_atom(filename:basename(Path, ".erl"))) of
        true -> ok;
        false -> compile_source(Path, Options)
    end.

%% Modules of kernel, stdlib and compiler live in sticky directories and cannot be reloaded: a project source of
%% one (such as OTP's own qlc_pt.erl on a search path) is not compiled, the host's module is used.
host_sticky(Module) ->
    _ = code:ensure_loaded(Module),
    code:is_sticky(Module).

compile_source(Path, Options) ->
    case compile:file(Path, [binary, return_errors, return_warnings | Options]) of
        {ok, Module, Binary, _Warnings} ->
            {module, Module} = code:load_binary(Module, Path, Binary);
        {error, Errors, _Warnings} ->
            Hint = {
                Path,
                none,
                "cannot compile parse transform source; precompile it into a .beam with the host "
                "Erlang/OTP and pass its directory with --transform-path"
            },
            throw({reply, {error, messages(Errors) ++ [Hint], []}})
    end.

%% compile:foldl_transform/3: each transform gets the previous one's forms; the first error stops the chain.
transform([Transform | Rest], Forms0, Options, File, Warnings) ->
    case
        code:ensure_loaded(Transform) =:= {module, Transform} andalso
            erlang:function_exported(Transform, parse_transform, 2)
    of
        false ->
            {error, [{File, none, unavailable(Transform)}], Warnings};
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
                        {File, none,
                            compile_error({parse_transform, Transform, {Class, Reason, Stack}})},
                    {error, [Error], Warnings}
            end
    end;
transform([], Forms, Options, _File, Warnings) ->
    {ok, strip_columns_if(Forms, option_location(Options) =:= line), Warnings}.

%% Why a transform cannot run: a .beam on the code path that does not load is named, else OTP's undefined text.
unavailable(Transform) ->
    case {code:ensure_loaded(Transform), code:where_is_file(atom_to_list(Transform) ++ ".beam")} of
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
