%% Clause's erl_features (plan 21 step LIB8B, docs/builtins.md#compiler-tables): OTP 29's features as Clause's
%% compiler knows them; the queries are runtime builtins, used/1 and short/1 Erlang over them. The parse state
%% functions the compiler uses inside OTP stay stubs raising error({not_implemented, F, A}).
-module(erl_features).
-export([
    all/0,
    configurable/0,
    enabled/0,
    format_error/1,
    format_error/2,
    info/1,
    init_parse_state/2,
    keyword_fun/2,
    keyword_fun/4,
    keywords/0,
    keywords/1,
    long/1,
    short/1,
    update_parse_state/4,
    used/1
]).

all() -> clause_builtin_placeholder.
configurable() -> clause_builtin_placeholder.
enabled() -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
keywords() -> clause_builtin_placeholder.
keywords(_) -> clause_builtin_placeholder.

%% The features a module of the program was compiled with, or those a .beam file records; not_found otherwise.
used(Module) when is_atom(Module) ->
    clause_features:module_features(Module);
used(FileName) when is_list(FileName) ->
    case file:read_file(filename:rootname(FileName, ".beam") ++ ".beam") of
        {ok, Beam} -> beam_features(Beam);
        {error, _} -> not_found
    end.

% The enabled_features of a BEAM file's Meta chunk, gzip-compressed files included; [] without one.
beam_features(<<31, 139, _/binary>> = Compressed) ->
    try zlib:gunzip(Compressed) of
        Beam -> beam_features(Beam)
    catch
        _:_ -> not_found
    end;
beam_features(Beam) ->
    case clause_features:beam_meta(Beam) of
        missing -> [];
        error -> not_found;
        Meta -> proplists:get_value(enabled_features, binary_to_term(Meta), [])
    end.

%% One line describing a feature: its short description, status and the release of that status.
short(Feature) ->
    #{short := Short, status := Status} = Info = erl_features:info(Feature),
    #{Status := Release} = Info,
    io_lib:format("~-40s ~-12s (~p)", [Short, Status, Release]).

format_error(_) -> erlang:error({not_implemented, format_error, 1}).
format_error(_, _) -> erlang:error({not_implemented, format_error, 2}).
init_parse_state(_, _) -> erlang:error({not_implemented, init_parse_state, 2}).
keyword_fun(_, _) -> erlang:error({not_implemented, keyword_fun, 2}).
keyword_fun(_, _, _, _) -> erlang:error({not_implemented, keyword_fun, 4}).
long(_) -> erlang:error({not_implemented, long, 1}).
update_parse_state(_, _, _, _) -> erlang:error({not_implemented, update_parse_state, 4}).
