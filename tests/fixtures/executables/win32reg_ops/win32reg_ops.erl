-module(win32reg_ops).
-export([main/1]).

%% win32reg (plan 21 step LIB55B): on Windows, keys made, walked (absolute,
%% relative and .. paths), valued with integers, strings and binaries, read,
%% listed and deleted again under HKEY_CURRENT_USER\Software\ClauseWin32regTest
%% (one key per process, as several runs may go at once; output names it
%% without the process id), with OTP's errors; elsewhere open/1 is
%% {error, enotsup}. expand/1 and format_error/1 everywhere.
main([]) ->
    put(key, "ClauseWin32regTest" ++ os:getpid()),
    show(expand, [
        win32reg:expand("100%% sure"),
        win32reg:expand("a%CLAUSE_WIN32REG_UNSET%b"),
        win32reg:expand("no variables")
    ]),
    show(format_error, [win32reg:format_error(enoent), win32reg:format_error(eacces)]),
    case os:type() of
        {win32, _} ->
            registry();
        _ ->
            show(open, [win32reg:open([read]), win32reg:open([read, write]), win32reg:open([bogus])])
    end,
    io:format("win32reg_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, scrub(Value)]).

%% The test key's name without the process id.
scrub([C | _] = String) when is_integer(C) ->
    lists:flatten(string:replace(String, get(key), "ClauseWin32regTest", all));
scrub(List) when is_list(List) ->
    [scrub(E) || E <- List];
scrub(Tuple) when is_tuple(Tuple) ->
    list_to_tuple(scrub(tuple_to_list(Tuple)));
scrub(Other) ->
    Other.

registry() ->
    {ok, R} = win32reg:open([read, write]),
    show(initial, win32reg:current_key(R)),
    show(software, [win32reg:change_key(R, "\\hkcu\\Software"), win32reg:current_key(R)]),
    show(create, [win32reg:change_key_create(R, get(key) ++ "\\Sub"), win32reg:current_key(R)]),
    show(set, [
        win32reg:set_value(R, "dword", 42),
        win32reg:set_value(R, "string", "text"),
        win32reg:set_value(R, "binary", <<1, 2, 3>>),
        win32reg:set_value(R, default, "default value")
    ]),
    show(read, [
        win32reg:value(R, "dword"),
        win32reg:value(R, "string"),
        win32reg:value(R, "binary"),
        win32reg:value(R, default),
        win32reg:value(R, "missing")
    ]),
    {ok, Values} = win32reg:values(R),
    show(values, lists:sort(Values)),
    show(parent, [win32reg:change_key(R, ".."), win32reg:current_key(R), win32reg:sub_keys(R)]),
    show(relative, [win32reg:change_key(R, "Sub"), win32reg:current_key(R)]),
    show(delete_value, [
        win32reg:delete_value(R, "string"),
        win32reg:value(R, "string"),
        win32reg:delete_value(R, "string")
    ]),
    show(delete_keys, [
        win32reg:delete_key(R),
        win32reg:current_key(R),
        win32reg:sub_keys(R),
        win32reg:delete_key(R),
        win32reg:current_key(R)
    ]),
    show(errors, [
        win32reg:change_key(R, "\\nosuchroot\\x"),
        win32reg:change_key(R, "\\hkcu\\ClauseWin32regNoSuchKey"),
        win32reg:current_key(R),
        win32reg:change_key(R, "..\\..\\..\\.."),
        win32reg:change_key(R, "\\hkey_current_user"),
        win32reg:delete_key(R),
        win32reg:open([bogus])
    ]),
    ok = win32reg:close(R),
    {ok, ReadOnly} = win32reg:open([read]),
    show(read_only, [
        win32reg:change_key(ReadOnly, "\\hkcu\\Software"), win32reg:set_value(ReadOnly, "x", 1)
    ]),
    ok = win32reg:close(ReadOnly).
