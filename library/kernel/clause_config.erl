%% The -config files of a program (sys.config, docs/executables.md#args-files), read as OTP's application_controller
%% reads them at boot (check_conf): the files of init's config flags in order, merged, sys.config naming other files.
%% Added for -config (plan 30 step DIST4a4).
-module(clause_config).
-export([read/0, merge_app_env/2]).

%% {ok, Config} with every file merged, or {error, Text}: the reason OTP's boot fails with.
read() ->
    try
        {ok, lists:foldl(fun(File, Env) -> merge_env(Env, file_config(File)) end, [], files())}
    catch
        throw:{error, {File, Line, Str}} ->
            {error, lists:flatten(io_lib:format("error in config file ~tp (~w): ~ts", [File, Line, to_string(Str)]))}
    end.

%% Every value of every config flag; a flag without one names the file "" as OTP's does.
files() -> lists:append([config_values(Values) || {config, Values} <- init:get_arguments()]).

config_values([]) -> [[]];
config_values(Values) -> Values.

%% One -config file, ".config" appended; sys.config may name other files, relative to its directory first.
file_config(FileName) ->
    BFName = filename:basename(FileName, ".config"),
    FName = filename:join(filename:dirname(FileName), BFName ++ ".config"),
    case load_file(FName) of
        {ok, NewEnv} when BFName =:= "sys" ->
            case check_conf_sys(NewEnv, [], [], filename:dirname(FName)) of
                {ok, SysEnv, []} -> SysEnv;
                {ok, _, [{error, {SysFName, Line, Str}} | _]} -> throw({error, {SysFName, Line, Str}})
            end;
        {ok, NewEnv} ->
            NewEnv;
        {error, {Line, _Mod, Str}} ->
            throw({error, {FName, Line, Str}})
    end.

check_conf_sys([File | T], SysEnv, Errors, DName) when is_list(File), is_list(DName) ->
    LName = included_file(File, DName),
    case load_file(LName) of
        {ok, NewEnv} -> check_conf_sys(T, merge_env(SysEnv, NewEnv), Errors, DName);
        {error, {Line, _Mod, Str}} -> check_conf_sys(T, SysEnv, [{error, {LName, Line, Str}} | Errors], DName)
    end;
check_conf_sys([Tuple | T], SysEnv, Errors, DName) ->
    check_conf_sys(T, merge_env(SysEnv, [Tuple]), Errors, DName);
check_conf_sys([], SysEnv, Errors, _) ->
    {ok, SysEnv, lists:reverse(Errors)}.

%% A file sys.config names: relative to sys.config's directory when it is there, else to the current directory.
included_file(File, DName) ->
    FName = filename:join(filename:dirname(File), filename:basename(File, ".config") ++ ".config"),
    RName = filename:join(DName, FName),
    case filename:pathtype(FName) =:= relative andalso DName =/= [] andalso filelib:is_file(RName) of
        true -> RName;
        false -> FName
    end.

load_file(File) ->
    case file:read_file(File) of
        {ok, Bin} ->
            % Some whitespace after the last dot, so a file without a final newline reads.
            case file_binary_to_list(Bin) of
                {ok, String} -> scan_file(String ++ " ");
                error -> {error, {none, scan_file, "bad encoding"}}
            end;
        {error, _} ->
            {error, {none, open_file, "configuration file not found"}}
    end.

file_binary_to_list(Bin) ->
    Enc =
        case epp:read_encoding_from_binary(Bin) of
            none -> epp:default_encoding();
            Encoding -> Encoding
        end,
    case catch unicode:characters_to_list(Bin, Enc) of
        String when is_list(String) -> {ok, String};
        _ -> error
    end.

scan_file(Str) ->
    case erl_scan:tokens([], Str, 1) of
        {done, {ok, Tokens, _}, Left} ->
            case erl_parse:parse_term(Tokens) of
                {ok, L} = Res when is_list(L) ->
                    case only_ws(Left) of
                        true -> Res;
                        false -> config_error()
                    end;
                {ok, _} ->
                    config_error();
                Error ->
                    Error
            end;
        {done, {error, {Line, Module, Description}, _}, _} ->
            {error, {Line, parse_file, {Module, Description}}};
        {more, _} ->
            {error, {none, load_file, "no ending <dot> found"}}
    end.

only_ws([C | Cs]) when C =< $\s -> only_ws(Cs);
only_ws([$% | Cs]) -> only_ws(strip_comment(Cs));
only_ws([_ | _]) -> false;
only_ws([]) -> true.

strip_comment([$\n | Cs]) -> Cs;
strip_comment([_ | Cs]) -> strip_comment(Cs);
strip_comment([]) -> [].

config_error() ->
    {error, {none, load_file, "configuration file must contain ONE list ended by <dot>"}}.

%% OTP's merge of configurations: an application's parameters merged as merge_app_env does.
merge_env(Env1, Env2) -> merge_env(Env1, Env2, []).

merge_env([{App, AppEnv1} | T], Env2, Res) ->
    case get_env_key(App, Env2) of
        {value, AppEnv2, RestEnv2} -> merge_env(T, RestEnv2, [{App, merge_app_env(AppEnv1, AppEnv2)} | Res]);
        _ -> merge_env(T, Env2, [{App, AppEnv1} | Res])
    end;
merge_env([], Env2, Res) ->
    Env2 ++ Res.

%% OTP's merge of a configured or --env environment over an application's: the first value of a key the application
%% has wins, the last of a new key, and the result is reversed, which decides between --env duplicates as in OTP.
merge_app_env(Env1, Env2) -> merge_app_env(Env1, Env2, []).

merge_app_env([{Key, Val} | T], Env2, Res) ->
    case get_env_key(Key, Env2) of
        {value, NewVal, RestEnv} -> merge_app_env(T, RestEnv, [{Key, NewVal} | Res]);
        _ -> merge_app_env(T, Env2, [{Key, Val} | Res])
    end;
merge_app_env([], Env2, Res) ->
    Env2 ++ Res.

get_env_key(Key, Env) -> get_env_key(Env, Key, []).

get_env_key([{Key, Val} | T], Key, Res) -> {value, Val, T ++ Res};
get_env_key([H | T], Key, Res) -> get_env_key(T, Key, [H | Res]);
get_env_key([], _Key, Res) -> Res.

to_string(Term) ->
    case io_lib:printable_list(Term) of
        true -> Term;
        false -> lists:flatten(io_lib:format("~0p", [Term]))
    end.
