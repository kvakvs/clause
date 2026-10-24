%% Clause's erl_features (plan 21 step LIB8B, docs/builtins.md#compiler-tables): OTP 29's features as Clause's
%% compiler knows them; the queries are runtime builtins, used/1 and short/1 Erlang over them, and the parse state of
%% epp (init_parse_state/2, update_parse_state/4) and format_error/1,2 Erlang (LIB34). long/1 stays a stub.
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

%% The parse state of epp and the compiler (plan 21 step LIB34): the enabled features and the reserved word fun that
%% accepts their keywords.

%% format_error({Error, Features}): the words of a feature error.
format_error({Error, Features}) ->
    Words =
        case {Error, Features} of
            {invalid_features, [_]} -> "the feature ~s does not exist.";
            {invalid_features, _} -> "the features ~s do not exist.";
            {not_configurable, [_]} -> "the feature ~s is not configurable.";
            {not_configurable, _} -> "the features ~s are not configurable.";
            {incorrect_features, _} -> "the features ~s do not exist or are not configurable."
        end,
    io_lib:fwrite(Words, [feature_names(Features)]).

feature_names([F]) -> io_lib:fwrite("'~p'", [F]);
feature_names([F1, F2]) -> io_lib:fwrite("'~p' and '~p'", [F1, F2]);
feature_names([F | Fs]) -> io_lib:fwrite("'~p', ~s", [F, feature_names(Fs)]).

%% format_error(Reason, StackTrace): erl_error's extra information, the cause of the error_info with the reason.
format_error(Reason, [{_M, _F, _Args, Info} | _]) ->
    Cause = maps:get(cause, proplists:get_value(error_info, Info, #{})),
    Cause#{reason => io_lib:format("~p: ~p", [?MODULE, Reason])}.

keyword_fun(Opts, KeywordFun) -> init_parse_state(Opts, KeywordFun).

keyword_fun(Ind, Feature, Ftrs, KeywordFun) -> update_parse_state(Ind, Feature, Ftrs, KeywordFun).

%% init_parse_state(Options, KeywordFun): the features {feature, F, enable | disable} options leave enabled, starting
%% from enabled/0, and KeywordFun extended to their keywords.
init_parse_state(Opts, KeywordFun) ->
    Ops = [Op || {feature, _, Ind} = Op <- Opts, Ind =:= enable orelse Ind =:= disable],
    {Added, Removed, Named} = collect_features(Ops, erl_features:enabled(), [], []),
    case lists:all(fun is_configurable/1, Named) of
        true ->
            Fun = lists:foldl(
                fun remove_keywords/2, lists:foldl(fun add_keywords/2, KeywordFun, Added), Removed
            ),
            {ok, {Added -- Removed, Fun}};
        false ->
            feature_error(Named)
    end.

%% update_parse_state(Ind, Feature, Features, KeywordFun): -feature(Feature, Ind) applied.
update_parse_state(Ind, Feature, Ftrs, KeywordFun) ->
    case is_configurable(Feature) of
        true when Ind =:= enable ->
            Enabled =
                case lists:member(Feature, Ftrs) of
                    true -> Ftrs;
                    false -> [Feature | Ftrs]
                end,
            {ok, {Enabled, add_keywords(Feature, KeywordFun)}};
        true ->
            {ok, {Ftrs -- [Feature], remove_keywords(Feature, KeywordFun)}};
        false ->
            Error =
                case lists:member(Feature, erl_features:all()) of
                    true -> not_configurable;
                    false -> invalid_features
                end,
            {error, {?MODULE, {Error, [Feature]}}}
    end.

add_keywords(Feature, F) ->
    Words = erl_features:keywords(Feature),
    fun(Word) -> lists:member(Word, Words) orelse F(Word) end.

remove_keywords(Feature, F) ->
    Words = erl_features:keywords(Feature),
    fun(Word) -> not lists:member(Word, Words) andalso F(Word) end.

is_configurable(Feature) -> lists:member(Feature, erl_features:configurable()).

feature_error(Features) ->
    Invalid = [F || F <- Features, not lists:member(F, erl_features:all())],
    NonConfigurable = [
        F
     || F <- Features, lists:member(F, erl_features:all()), not is_configurable(F)
    ],
    {Error, Culprits} =
        case {Invalid, NonConfigurable} of
            {[], NC} -> {not_configurable, NC};
            {NV, []} -> {invalid_features, NV};
            {NV, NC} -> {incorrect_features, NV ++ NC}
        end,
    {error, {?MODULE, {Error, Culprits}}}.

%% The features added and removed by the options in order, and those named; all enables every configurable one.
collect_features([], Added, Removed, Named) ->
    {Added, Removed, Named};
collect_features([{feature, all, enable} | Ops], Added, _Removed, Named) ->
    collect_features(
        Ops, lists:foldl(fun append_new/2, Added, erl_features:configurable()), [], Named
    );
collect_features([{feature, Feature, enable} | Ops], Added, Removed, Named) ->
    collect_features(Ops, append_new(Feature, Added), Removed -- [Feature], Named ++ [Feature]);
collect_features([{feature, all, disable} | Ops], _Added, Removed, Named) ->
    collect_features(Ops, [], Removed -- erl_features:configurable(), Named);
collect_features([{feature, Feature, disable} | Ops], Added, Removed, Named) ->
    collect_features(Ops, Added -- [Feature], append_new(Feature, Removed), Named ++ [Feature]).

append_new(F, Fs) ->
    case lists:member(F, Fs) of
        true -> Fs;
        false -> Fs ++ [F]
    end.

long(_) -> erlang:error({not_implemented, long, 1}).
