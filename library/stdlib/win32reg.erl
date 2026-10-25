%% Clause's win32reg (plan 21 step LIB55B, docs/library.md): the Windows registry, written from OTP's documentation and
%% following OTP's key parsing, current key and value encodings. OTP's registry driver answers commands with port
%% messages; Clause's (runtime/src/ports/registry_windows.cpp) answers the same replies through port_control/3.
%% Elsewhere open/1 is {error, enotsup}, as OTP's.
-module(win32reg).
-export([
    change_key/2,
    change_key_create/2,
    close/1,
    current_key/1,
    delete_key/1,
    delete_value/2,
    expand/1,
    format_error/1,
    open/1,
    set_value/3,
    sub_keys/1,
    value/2,
    values/1
]).
-include("win32reg.types.hrl").
%% The registry's command atoms and reasons never name a called module.
-compile({clause_liveness, strict}).

% The predefined root keys.
-define(hkey_classes_root, 16#80000000).
-define(hkey_current_user, 16#80000001).
-define(hkey_local_machine, 16#80000002).
-define(hkey_users, 16#80000003).
-define(hkey_performance_data, 16#80000004).
-define(hkey_current_config, 16#80000005).
-define(hkey_dyn_data, 16#80000006).

% The driver's operations, as OTP's commands, and the access mode set at open.
-define(cmd_get_current, 0).
-define(cmd_open_key, 1).
-define(cmd_create_key, 2).
-define(cmd_get_all_subkeys, 3).
-define(cmd_get_value, 4).
-define(cmd_get_all_values, 5).
-define(cmd_set_value, 6).
-define(cmd_delete_key, 7).
-define(cmd_delete_value, 8).
-define(cmd_set_mode, 9).

% The registry value types.
-define(reg_sc, 1).
-define(reg_expand_sc, 2).
-define(reg_binary, 3).
-define(reg_dword, 4).

open(Modes) ->
    case os:type() of
        {win32, _} ->
            case open_mode(Modes, []) of
                {error, Reason} ->
                    {error, Reason};
                ModeStr ->
                    P = open_port({spawn_driver, "clause_registry"}, [binary]),
                    _ = port_control(P, ?cmd_set_mode, ModeStr),
                    {ok, {win32reg, P}}
            end;
        _ ->
            {error, enotsup}
    end.

close({win32reg, Reg}) when is_port(Reg) ->
    unlink(Reg),
    exit(Reg, die),
    ok.

current_key({win32reg, Reg}) when is_port(Reg) ->
    {state, Hkey, Name} = request(Reg, ?cmd_get_current, []),
    Root = hkey_to_string(Hkey),
    {ok,
        case Name of
            [] -> Root;
            _ -> Root ++ [$\\ | Name]
        end}.

change_key({win32reg, Reg}, Key) when is_port(Reg) -> change_key(Reg, ?cmd_open_key, Key).

change_key_create({win32reg, Reg}, Key) when is_port(Reg) -> change_key(Reg, ?cmd_create_key, Key).

change_key(Reg, Cmd, Key) ->
    case parse_key(Key, Reg) of
        {ok, Hkey, Path} -> request(Reg, Cmd, [i32(Hkey), Path, 0]);
        {error, Reason} -> {error, Reason}
    end.

sub_keys({win32reg, Reg}) when is_port(Reg) -> collect(Reg, ?cmd_get_all_subkeys, key).

% The current key deleted from its parent, which becomes the current key; a root key cannot be deleted.
delete_key({win32reg, Reg}) when is_port(Reg) ->
    case request(Reg, ?cmd_get_current, []) of
        {state, _Hkey, []} ->
            {error, eperm};
        {state, _Hkey, Rest} ->
            [Name | _] = split_key(Rest),
            ok = change_key(Reg, ?cmd_open_key, ".."),
            request(Reg, ?cmd_delete_key, [Name, 0]);
        {error, Error} ->
            {error, Error}
    end.

set_value({win32reg, Reg}, Name0, Value) when is_port(Reg) ->
    {Type, V} = term_to_value(Value),
    request(Reg, ?cmd_set_value, [Type, value_name(Name0), 0, V]).

value({win32reg, Reg}, Name0) when is_port(Reg) ->
    Name = value_name(Name0),
    case request(Reg, ?cmd_get_value, [Name, 0]) of
        {value, {default, Value}} when Name =:= [] -> {ok, Value};
        {value, {Name, Value}} -> {ok, Value};
        {error, Reason} -> {error, Reason}
    end.

values({win32reg, Reg}) when is_port(Reg) -> collect(Reg, ?cmd_get_all_values, value).

delete_value({win32reg, Reg}, Name0) when is_port(Reg) -> request(Reg, ?cmd_delete_value, [value_name(Name0), 0]).

value_name(default) -> [];
value_name(Name) -> Name.

% Environment variables between percent signs replaced by their values; %% is a percent sign.
expand(Value) -> expand(Value, [], []).

expand([$%, $% | Rest], [], Result) -> expand(Rest, [], [$% | Result]);
expand([$%, C | Rest], [], Result) -> expand(Rest, [C], Result);
expand([C | Rest], [], Result) -> expand(Rest, [], [C | Result]);
expand([$% | Rest], Env0, Result) -> expand(Rest, [], lists:reverse(os:getenv(lists:reverse(Env0), "")) ++ Result);
expand([C | Rest], Env, Result) -> expand(Rest, [C | Env], Result);
expand([], [], Result) -> lists:reverse(Result).

format_error(ErrorId) -> erl_posix_msg:message(ErrorId).

% --- The driver ---

% One reply of an operation.
request(Reg, Op, Data) -> get_result1(binary_to_list(port_control(Reg, Op, Data))).

% The replies of a listing, each preceded by its 32-bit size, the last ok or an error.
collect(Reg, Op, Kind) -> collect_replies(binary_to_list(port_control(Reg, Op, [])), Kind, []).

collect_replies(Bytes, Kind, Result) ->
    {ok, Size, Rest} = i32_on_head(Bytes),
    {Reply, Next} = lists:split(Size, Rest),
    case get_result1(Reply) of
        ok -> {ok, lists:reverse(Result)};
        {Kind, Data} -> collect_replies(Next, Kind, [Data | Result]);
        {error, Reason} -> {error, Reason}
    end.

get_result1([$e | Reason]) ->
    {error, list_to_atom(Reason)};
get_result1([$o]) ->
    ok;
get_result1([$k | Name]) ->
    {key, Name};
get_result1([$v | Rest0]) ->
    {ok, Type, Rest1} = i32_on_head(Rest0),
    {ok, Name0, Value} = get_cstring(Rest1),
    Name =
        case Name0 of
            [] -> default;
            _ -> Name0
        end,
    {value, {Name, encode_value(Type, Value)}};
get_result1([$s | Rest0]) ->
    {ok, Hkey, Name} = i32_on_head(Rest0),
    {state, Hkey, Name}.

encode_value(?reg_sc, Value) -> Value;
encode_value(?reg_expand_sc, Value) -> Value;
encode_value(?reg_dword, Value) -> i32(Value);
encode_value(_, Value) -> list_to_binary(Value).

term_to_value(Int) when is_integer(Int) -> {i32(?reg_dword), i32(Int)};
term_to_value(String) when is_list(String) -> {i32(?reg_sc), [String, 0]};
term_to_value(Bin) when is_binary(Bin) -> {i32(?reg_binary), Bin};
term_to_value(_) -> exit(badarg).

get_cstring(List) -> get_cstring(List, []).

get_cstring([0 | Rest], Result) -> {ok, lists:reverse(Result), Rest};
get_cstring([C | Rest], Result) -> get_cstring(Rest, [C | Result]);
get_cstring([], Result) -> {ok, lists:reverse(Result), []}.

i32(Int) when is_integer(Int) -> [(Int bsr 24) band 255, (Int bsr 16) band 255, (Int bsr 8) band 255, Int band 255];
i32([X1, X2, X3, X4]) -> (X1 bsl 24) bor (X2 bsl 16) bor (X3 bsl 8) bor X4.

i32_on_head([X1, X2, X3, X4 | Rest]) -> {ok, (X1 bsl 24) bor (X2 bsl 16) bor (X3 bsl 8) bor X4, Rest}.

% --- Key paths ---

% An absolute key (\Root\Path) or one relative to the current key, where .. goes up.
parse_key([$\\ | Rest], _) -> parse_root(Rest, []);
parse_key(Key, Reg) -> parse_relative(Key, Reg).

parse_relative(Path, Reg) ->
    {state, RootHandle, Name} = request(Reg, ?cmd_get_current, []),
    Original = split_key(Name),
    Relative = lists:reverse(split_key(Path)),
    case parse_relative1(Relative, Original) of
        {error, Error} -> {error, Error};
        NewPath -> {ok, RootHandle, NewPath}
    end.

parse_relative1([".." | _], []) -> {error, enoent};
parse_relative1([".." | T1], [_ | T2]) -> parse_relative1(T1, T2);
parse_relative1([Comp | Rest], Result) -> parse_relative1(Rest, [Comp | Result]);
parse_relative1([], Result) -> reverse_and_join(Result, []).

reverse_and_join([X | Rest], []) -> reverse_and_join(Rest, [X]);
reverse_and_join([X | Rest], Result) -> reverse_and_join(Rest, [X, "\\" | Result]);
reverse_and_join([], Result) -> Result.

% The components of a key, last first.
split_key(Key) -> split_key(Key, [], []).

split_key([$\\ | Rest], Current, Result) -> split_key(Rest, [], [lists:reverse(Current) | Result]);
split_key([C | Rest], Current, Result) -> split_key(Rest, [C | Current], Result);
split_key([], [], Result) -> Result;
split_key([], Current, Result) -> [lists:reverse(Current) | Result].

parse_root([$\\ | Rest], Result) ->
    Root =
        case lists:reverse(Result) of
            [$h, $k, $e, $y, $_ | Root0] -> Root0;
            Root0 -> Root0
        end,
    case root_to_handle(list_to_atom(Root)) of
        false -> {error, enoent};
        Handle -> {ok, Handle, Rest}
    end;
parse_root([C | Rest], Result) ->
    parse_root(Rest, [C | Result]);
parse_root([], Result) ->
    parse_root([$\\], Result).

root_to_handle(classes_root) -> ?hkey_classes_root;
root_to_handle(hkcr) -> ?hkey_classes_root;
root_to_handle(current_user) -> ?hkey_current_user;
root_to_handle(hkcu) -> ?hkey_current_user;
root_to_handle(local_machine) -> ?hkey_local_machine;
root_to_handle(hklm) -> ?hkey_local_machine;
root_to_handle(users) -> ?hkey_users;
root_to_handle(hku) -> ?hkey_users;
root_to_handle(current_config) -> ?hkey_current_config;
root_to_handle(hkcc) -> ?hkey_current_config;
root_to_handle(dyn_data) -> ?hkey_dyn_data;
root_to_handle(hkdd) -> ?hkey_dyn_data;
root_to_handle(performance_data) -> ?hkey_performance_data;
root_to_handle(_) -> false.

hkey_to_string(?hkey_classes_root) -> "\\hkey_classes_root";
hkey_to_string(?hkey_current_user) -> "\\hkey_current_user";
hkey_to_string(?hkey_local_machine) -> "\\hkey_local_machine";
hkey_to_string(?hkey_users) -> "\\hkey_users";
hkey_to_string(?hkey_performance_data) -> "\\hkey_performance_data";
hkey_to_string(?hkey_current_config) -> "\\hkey_current_config";
hkey_to_string(?hkey_dyn_data) -> "\\hkey_dyn_data".

open_mode([read | Rest], Result) -> open_mode(Rest, [$r | Result]);
open_mode([write | Rest], Result) -> open_mode(Rest, [$w | Result]);
open_mode([], Result) -> Result;
open_mode(_, _) -> {error, einval}.
