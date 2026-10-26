%% Clause's replacement for OTP's re (docs/libraries/library.md#runtime-module-replacements), generated from
%% re.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}); import/1 stays one, as Clause exports no patterns.
-module(re).
-export([
    compile/1,
    compile/2,
    grun/3,
    import/1,
    inspect/2,
    internal_run/4,
    replace/3,
    replace/4,
    run/2,
    run/3,
    split/2,
    split/3,
    ucompile/2,
    urun/3,
    version/0
]).

-include("re.types.hrl").

%% Regular expressions over the pinned PCRE2 (plan step 41ZB, docs/builtins.md#re-module): clause_re:compile/2 and
%% clause_re:run/4 are the halves OTP implements in C; this module converts Unicode chardata to UTF-8, runs global
%% matches one match at a time and builds replace/split on run/3, with OTP's results.

%% --- Compiling ---

compile(RE) -> compile(RE, []).

compile(RE, Options) when is_list(Options) ->
    clause_re:compile(regex_bytes(RE, lists:member(unicode, Options), [RE, Options]), Options);
compile(RE, Options) ->
    erlang:error(badarg, [RE, Options]).

%% Exported patterns (OTP 28.1) are not provided.
import(_) -> erlang:error({not_implemented, import, 1}).

ucompile(RE, Options) -> compile(unicode:characters_to_binary(RE, unicode), Options).

%% A regex as bytes: binaries as they are, Unicode chardata as UTF-8, other iodata flattened; badarg otherwise.
regex_bytes(RE, _, _) when is_binary(RE) -> RE;
regex_bytes(RE, true, Arguments) -> utf8(RE, Arguments);
regex_bytes(RE, false, Arguments) -> bytes(RE, Arguments).

utf8(Data, Arguments) ->
    case unicode:characters_to_binary(Data, unicode) of
        Binary when is_binary(Binary) -> Binary;
        _ -> erlang:error(badarg, Arguments)
    end.

bytes(Data, Arguments) ->
    try
        iolist_to_binary(Data)
    catch
        error:_ -> erlang:error(badarg, Arguments)
    end.

%% --- Matching ---

run(Subject, RE) -> run(Subject, RE, []).

%% A Unicode run on chardata converts it first; a global run matches repeatedly; anything else matches once.
run(Subject, RE, Options) when is_list(Options) ->
    Unicode = is_unicode(RE, Options),
    case Unicode andalso needs_conversion(Subject, RE, Options) of
        true -> urun(Subject, RE, Options);
        false -> run_bytes(Subject, RE, Options)
    end;
run(Subject, RE, Options) ->
    erlang:error(badarg, [Subject, RE, Options]).

run_bytes(Subject, RE, Options) ->
    case lists:member(global, Options) of
        true ->
            global_run(Subject, RE, Options);
        false ->
            clause_re:run(
                bytes(Subject, [Subject, RE, Options]),
                regex_term(RE, Subject, Options),
                Options,
                true
            )
    end.

internal_run(Subject, RE, Options, First) ->
    clause_re:run(
        bytes(Subject, [Subject, RE, Options]), regex_term(RE, Subject, Options), Options, First
    ).

%% A compiled pattern as it is, a textual regex as bytes; badarg for anything else.
regex_term({re_pattern, _, _, _, _} = Pattern, _, _) ->
    Pattern;
regex_term(RE, Subject, Options) when is_binary(RE); is_list(RE) ->
    bytes(RE, [Subject, RE, Options]);
regex_term(RE, Subject, Options) ->
    erlang:error(badarg, [Subject, RE, Options]).

is_unicode({re_pattern, _, Unicode, _, _}, _) -> Unicode =:= 1;
is_unicode(_, Options) -> lists:member(unicode, Options).

is_crlf({re_pattern, _, _, CRLF, _}) -> CRLF =:= 1.

%% Whether a Unicode run must convert first: a non-binary subject or textual regex, or list captures of one match.
needs_conversion(Subject, RE, Options) ->
    not is_binary(Subject) orelse (not is_tuple(RE) andalso not is_binary(RE)) orelse
        (capture_type(Options) =:= list andalso not lists:member(global, Options)).

capture_type([{capture, _, Type} | _]) -> Type;
capture_type([_ | Options]) -> capture_type(Options);
capture_type([]) -> index.

%% A Unicode run: subject and regex as UTF-8, captures of one match converted from byte offsets to the type asked.
urun(Subject0, RE0, Options0) ->
    {Options, Type} =
        case lists:member(global, Options0) of
            true -> {Options0, index};
            false -> unicode_options(Options0, index)
        end,
    Subject = utf8(Subject0, [Subject0, RE0, Options0]),
    RE =
        case RE0 of
            {re_pattern, _, _, _, _} -> RE0;
            _ -> regex_bytes(RE0, true, [Subject0, RE0, Options0])
        end,
    case {run(Subject, RE, Options), Type} of
        {{match, Captures}, binary} -> {match, [part(Subject, Capture) || Capture <- Captures]};
        {{match, Captures}, list} -> {match, [characters(Subject, Capture) || Capture <- Captures]};
        {Result, _} -> Result
    end.

%% The options with {capture, Values, Type} as {capture, Values}, and that Type.
unicode_options([{capture, Values, Type} | Options], _) ->
    unicode_options([{capture, Values} | Options], Type);
unicode_options([Option | Options], Type) ->
    {Rest, Last} = unicode_options(Options, Type),
    {[Option | Rest], Last};
unicode_options([], Type) ->
    {[], Type}.

part(_, {-1, 0}) -> <<>>;
part(Subject, {Start, Length}) -> binary:part(Subject, Start, Length).

characters(_, {_, 0}) ->
    [];
characters(Subject, {Start, Length}) ->
    unicode:characters_to_list(binary:part(Subject, Start, Length), unicode).

%% grun/3 is the trap target of OTP's BIF; here it runs the same global match.
grun(Subject, RE, {Options, _}) -> global_run(Subject, RE, Options);
grun(Subject, RE, {Options, _, _}) -> global_run(Subject, RE, Options).

%% --- Global matching ---

%% Every match from the offset on: a textual regex is compiled once with the compile options, which the matches
%% then leave out.
global_run(Subject, RE, Options) ->
    case global_pattern(Subject, RE, Options) of
        {error, _} = Error ->
            Error;
        {Pattern, Clean} ->
            Unicode = is_unicode(Pattern, Options),
            Flat =
                case Unicode of
                    true -> utf8(Subject, [Subject, RE, Options]);
                    false -> bytes(Subject, [Subject, RE, Options])
                end,
            {RunOptions, Offset, Select, Type} = global_options(Options, Clean, Pattern, [
                Subject, RE, Options
            ]),
            try
                Matches = matches(
                    Flat,
                    Pattern,
                    Offset,
                    byte_size(Flat),
                    {Unicode, is_crlf(Pattern)},
                    RunOptions,
                    true
                ),
                selected(Matches, Select, Type, Flat, Unicode)
            catch
                throw:{error, _} = Error -> Error
            end
    end.

global_pattern(_, {re_pattern, _, _, _, _} = Pattern, _) ->
    {Pattern, false};
global_pattern(Subject, RE, Options) ->
    CompileOptions = [Option || Option <- Options, not is_run_option(Option)],
    case
        clause_re:compile(
            regex_bytes(RE, lists:member(unicode, Options), [Subject, RE, Options]), CompileOptions
        )
    of
        {ok, Pattern} ->
            {Pattern, CompileOptions =/= [] andalso lists:any(fun is_compile_option/1, Options)};
        {error, Reason} ->
            case lists:member(report_errors, Options) of
                true -> {error, {compile, Reason}};
                false -> erlang:error(badarg, [Subject, RE, Options])
            end
    end.

%% {Options of each match, start offset, what to select, capture type}: global and offset come out; a capture
%% spec becomes one that keeps the whole match first, so the loop can step over it.
global_options(Options, Clean, Pattern, Arguments) ->
    try
        global_options(Options, Clean, Pattern, 0, false, index)
    catch
        throw:bad_options -> erlang:error(badarg, Arguments)
    end.

global_options([], _, _, Offset, Select, Type) ->
    {[], Offset, Select, Type};
global_options([{offset, N} | Rest], Clean, Pattern, _, Select, Type) ->
    global_options(Rest, Clean, Pattern, N, Select, Type);
global_options([global | Rest], Clean, Pattern, Offset, Select, Type) ->
    global_options(Rest, Clean, Pattern, Offset, Select, Type);
global_options([{capture, Values, Type} | Rest], Clean, Pattern, Offset, Select, _) ->
    global_options([{capture, Values} | Rest], Clean, Pattern, Offset, Select, Type);
global_options([{capture, Values} | Rest], Clean, Pattern, Offset0, Select0, Type0) ->
    case global_options(Rest, Clean, Pattern, Offset0, Select0, Type0) of
        {Tail, Offset, false, _} ->
            {Capture, Select} = global_capture(Values, Pattern),
            {[{capture, Capture} | Tail], Offset, Select, Type0};
        Later ->
            Later
    end;
global_options([Option | Rest], Clean, Pattern, Offset0, Select0, Type0) ->
    {Tail, Offset, Select, Type} = global_options(Rest, Clean, Pattern, Offset0, Select0, Type0),
    case Clean andalso is_compile_option(Option) of
        true -> {Tail, Offset, Select, Type};
        false -> {[Option | Tail], Offset, Select, Type}
    end;
global_options(_, _, _, _, _, _) ->
    throw(bad_options).

global_capture(all, _) ->
    {all, all};
global_capture(first, _) ->
    {first, all};
global_capture(all_but_first, _) ->
    {all, strip_first};
global_capture(none, _) ->
    {first, none};
global_capture([], _) ->
    {first, none};
global_capture(all_names, Pattern) ->
    case re:inspect(Pattern, namelist) of
        {namelist, []} -> {first, none};
        {namelist, Names} -> {[0 | Names], strip_first}
    end;
global_capture(List, _) when is_list(List) -> {[0 | List], strip_first};
global_capture(_, _) ->
    throw(bad_options).

%% The captures of each match from Offset to Size: after an empty match the next try at the same place must not be
%% empty and anchored, else the search steps over one character (a CR LF pair with CRLF newlines).
matches(_, _, Offset, Size, _, _, _) when Offset > Size ->
    [];
matches(Subject, Pattern, Offset, Size, Mode, Options, First) ->
    case internal_run(Subject, Pattern, [{offset, Offset} | Options], First) of
        {error, _} = Error ->
            throw(Error);
        nomatch ->
            [];
        {match, [{Start, Length} | _] = Captures} when Length > 0 ->
            [
                Captures
                | distinct(
                    Captures, matches(Subject, Pattern, Start + Length, Size, Mode, Options, false)
                )
            ];
        {match, [{Start, 0} | _] = Captures} ->
            [
                Captures
                | distinct(
                    Captures,
                    after_empty(Subject, Pattern, Offset, Start, Size, Mode, Options, Captures)
                )
            ]
    end.

%% The later matches without a repeat of the one before them.
distinct(Captures, [Captures | Rest]) -> Rest;
distinct(_, Rest) -> Rest.

after_empty(Subject, Pattern, Offset, Start, Size, Mode, Options, Captures) ->
    Retry =
        case
            internal_run(
                Subject, Pattern, [{offset, Offset}, notempty_atstart, anchored | Options], false
            )
        of
            {match, Captures} -> [];
            {match, Other} -> Other;
            _ -> []
        end,
    Next =
        case Retry of
            [{_, Step} | _] when Step > 0 -> Start + Step;
            _ when Offset =:= Start orelse Start =:= Size -> step(Subject, Start, Mode);
            _ -> Start
        end,
    Rest = matches(Subject, Pattern, Next, Size, Mode, Options, false),
    case Retry of
        [] -> Rest;
        _ -> [Retry | Rest]
    end.

%% The offset one character after At: a whole UTF-8 character in Unicode mode, both bytes of CR LF with CRLF
%% newlines.
step(Subject, At, {_, true}) when
    byte_size(Subject) >= At + 2, binary_part(Subject, At, 2) =:= <<"\r\n">>
->
    At + 2;
step(Subject, At, {true, _}) when At < byte_size(Subject) ->
    case binary:at(Subject, At) of
        B when B >= 16#F0, B < 16#F8 -> At + 4;
        B when B >= 16#E0, B < 16#F0 -> At + 3;
        B when B >= 16#C0, B < 16#E0 -> At + 2;
        _ -> At + 1
    end;
step(_, At, _) ->
    At + 1.

%% A global result: nomatch without matches, match when nothing is captured, else the captures in the type asked.
selected([], _, _, _, _) ->
    nomatch;
selected(_, none, _, _, _) ->
    match;
selected(Matches, Select, Type, Flat, Unicode) ->
    Kept =
        case Select of
            strip_first -> [Captures || [_ | Captures] <- Matches];
            _ -> Matches
        end,
    {match, [[converted(Capture, Type, Flat, Unicode) || Capture <- Captures] || Captures <- Kept]}.

converted(Capture, index, _, _) -> Capture;
converted(Capture, binary, Flat, _) -> part(Flat, Capture);
converted(Capture, list, Flat, true) -> characters(Flat, Capture);
converted({_, 0}, list, _, false) -> [];
converted({Start, Length}, list, Flat, false) -> binary_to_list(Flat, Start + 1, Start + Length).

is_compile_option(Option) ->
    lists:member(Option, [
        caseless,
        no_start_optimize,
        never_utf,
        ucp,
        dollar_endonly,
        dotall,
        extended,
        firstline,
        multiline,
        no_auto_capture,
        dupnames,
        ungreedy,
        unicode
    ]).

is_run_option({offset, _}) ->
    true;
is_run_option({capture, _, _}) ->
    true;
is_run_option({capture, _}) ->
    true;
is_run_option({match_limit, _}) ->
    true;
is_run_option({match_limit_recursion, _}) ->
    true;
is_run_option(Option) ->
    lists:member(Option, [notempty, notempty_atstart, notbol, noteol, global, report_errors]).

%% --- Replacing ---

replace(Subject, RE, Replacement) -> replace(Subject, RE, Replacement, []).

%% Subject with every match (global) or the first replaced: \N, \gN, \g{N} and & name groups, \X escapes X; a fun
%% gets the whole match and the groups.
replace(Subject, RE, Replacement, Options) ->
    Arguments = [Subject, RE, Replacement, Options],
    {RunOptions, Return} = replace_options(Options, iodata, Arguments),
    Unicode = is_unicode(RE, Options),
    Flat = subject_bytes(Subject, Unicode, Arguments),
    Template = template(Replacement, Unicode, Arguments),
    Result =
        case run(Flat, RE, RunOptions) of
            nomatch ->
                Subject;
            {match, [First | _] = Matches} when is_list(First) ->
                substituted(Flat, Template, Matches);
            {match, Captures} ->
                substituted(Flat, Template, [Captures])
        end,
    returned(Result, Return, Unicode).

replace_options([], Return, _) ->
    {[], Return};
replace_options([{return, Return} | Rest], _, Arguments) when
    Return =:= iodata; Return =:= list; Return =:= binary
->
    replace_options(Rest, Return, Arguments);
replace_options([Option | Rest], Return, Arguments) when is_atom(Option); is_tuple(Option) ->
    case Option of
        report_errors ->
            erlang:error(badarg, Arguments);
        {capture, _} ->
            erlang:error(badarg, Arguments);
        {capture, _, _} ->
            erlang:error(badarg, Arguments);
        {return, _} ->
            erlang:error(badarg, Arguments);
        _ ->
            {Tail, Last} = replace_options(Rest, Return, Arguments),
            {[Option | Tail], Last}
    end;
replace_options(_, _, Arguments) ->
    erlang:error(badarg, Arguments).

subject_bytes(Subject, true, Arguments) -> utf8(Subject, Arguments);
subject_bytes(Subject, false, Arguments) -> bytes(Subject, Arguments).

returned(IoData, iodata, _) -> IoData;
returned(IoData, binary, false) -> iolist_to_binary(IoData);
returned(IoData, binary, true) -> unicode:characters_to_binary(IoData, unicode);
returned(IoData, list, false) -> binary_to_list(iolist_to_binary(IoData));
returned(IoData, list, true) -> unicode:characters_to_list(IoData, unicode).

%% A replacement as parts: binaries and group numbers; a fun as it is.
template(Replacement, _, _) when is_function(Replacement, 2) -> Replacement;
template(Replacement, Unicode, Arguments) -> parts(subject_bytes(Replacement, Unicode, Arguments)).

parts(<<>>) ->
    [];
parts(<<"\\g{", Rest/binary>>) when byte_size(Rest) > 0 ->
    {Digits, <<"}", After/binary>>} = digits(Rest),
    [list_to_integer(Digits) | parts(After)];
parts(<<"\\g", Rest/binary>>) when byte_size(Rest) > 0 ->
    {Digits, After} = digits(Rest),
    [list_to_integer(Digits) | parts(After)];
parts(<<$\\, X, Rest/binary>>) when X < $1; X > $9 -> literal(X, parts(Rest));
parts(<<$\\, Rest/binary>>) when byte_size(Rest) > 0 ->
    {Digits, After} = digits(Rest),
    [list_to_integer(Digits) | parts(After)];
parts(<<$&, Rest/binary>>) ->
    [0 | parts(Rest)];
parts(<<X, Rest/binary>>) ->
    literal(X, parts(Rest)).

literal(X, [Bytes | Rest]) when is_binary(Bytes) -> [<<X, Bytes/binary>> | Rest];
literal(X, Rest) -> [<<X>> | Rest].

digits(<<D, Rest/binary>>) when D >= $0, D =< $9 ->
    {More, After} = digits(Rest),
    {[D | More], After};
digits(Rest) ->
    {[], Rest}.

%% The subject with each match's bytes replaced, as iodata.
substituted(Flat, Template, Matches) -> spliced(Flat, Flat, 0, Template, Matches).

spliced(_, <<>>, _, _, []) ->
    [];
spliced(_, Rest, _, _, []) ->
    Rest;
spliced(Whole, Rest, At, Template, [[{Start, _} | _] | _] = Matches) when Start > At ->
    <<Kept:(Start - At)/binary, After/binary>> = Rest,
    [Kept | spliced(Whole, After, Start, Template, Matches)];
spliced(Whole, Rest, At, Template, [[{At, Length} | _] = Captures | Matches]) ->
    <<_:Length/binary, After/binary>> = Rest,
    [
        replacement(Whole, Template, Captures)
        | spliced(Whole, After, At + Length, Template, Matches)
    ].

replacement(Whole, Fun, [Match | Groups]) when is_function(Fun) ->
    Fun(binary:part(Whole, Match), [group(Whole, Group) || Group <- Groups]);
replacement(_, [Bytes], _) when is_binary(Bytes) ->
    Bytes;
replacement(Whole, Template, Captures) ->
    Groups = list_to_tuple(Captures),
    [
        case Part of
            N when is_integer(N), N >= tuple_size(Groups) -> <<>>;
            N when is_integer(N) -> group(Whole, element(N + 1, Groups));
            Bytes -> Bytes
        end
     || Part <- Template
    ].

group(Whole, {Start, Length}) when Start >= 0, Length > 0 -> binary:part(Whole, Start, Length);
group(_, _) -> <<>>.

%% --- Splitting ---

split(Subject, RE) -> split(Subject, RE, []).

%% The parts of Subject between matches, with the groups of each match after the part before it; {parts, N} limits
%% the count, trim drops trailing empty parts, group keeps each part with its groups.
split(Subject, RE, Options) ->
    Arguments = [Subject, RE, Options],
    {RunOptions, Return, Limit, Trim, Group} = split_options(
        Options, {iodata, -1, false, false}, Arguments
    ),
    Unicode = is_unicode(RE, Options),
    Flat = subject_bytes(Subject, Unicode, Arguments),
    {Pattern, Groups, MatchOptions} = split_pattern(RE, RunOptions, Arguments),
    case Flat of
        <<>> ->
            [];
        _ ->
            Parts =
                case run(Flat, Pattern, MatchOptions ++ [global]) of
                    nomatch when Group -> [[Flat]];
                    nomatch ->
                        [Flat];
                    {match, Matches} ->
                        trimmed(cut(Flat, 0, Matches, Groups, Limit, Group), Trim, Group)
                end,
            split_result(Parts, Return, Unicode, Group)
    end.

split_options([], {Return, Limit, Trim, Group}, _) ->
    {[], Return, Limit, Trim, Group};
split_options([trim | Rest], {Return, _, _, Group}, Arguments) ->
    split_options(Rest, {Return, -1, true, Group}, Arguments);
split_options([{parts, 0} | Rest], {Return, _, _, Group}, Arguments) ->
    split_options(Rest, {Return, -1, true, Group}, Arguments);
split_options([{parts, N} | Rest], {Return, _, _, Group}, Arguments) when is_integer(N), N >= 1 ->
    split_options(Rest, {Return, N - 1, false, Group}, Arguments);
split_options([{parts, infinity} | Rest], {Return, _, _, Group}, Arguments) ->
    split_options(Rest, {Return, -1, false, Group}, Arguments);
split_options([group | Rest], {Return, Limit, Trim, _}, Arguments) ->
    split_options(Rest, {Return, Limit, Trim, true}, Arguments);
split_options([{return, Return} | Rest], {_, Limit, Trim, Group}, Arguments) when
    Return =:= iodata; Return =:= list; Return =:= binary
->
    split_options(Rest, {Return, Limit, Trim, Group}, Arguments);
split_options([Option | Rest], State, Arguments) when is_atom(Option); is_tuple(Option) ->
    case Option of
        _ when Option =:= global; Option =:= report_errors -> erlang:error(badarg, Arguments);
        {parts, _} ->
            erlang:error(badarg, Arguments);
        {capture, _} ->
            erlang:error(badarg, Arguments);
        {capture, _, _} ->
            erlang:error(badarg, Arguments);
        {return, _} ->
            erlang:error(badarg, Arguments);
        _ ->
            {Tail, Return, Limit, Trim, Group} = split_options(Rest, State, Arguments),
            {[Option | Tail], Return, Limit, Trim, Group}
    end;
split_options(_, _, Arguments) ->
    erlang:error(badarg, Arguments).

%% The pattern to split with, its group count and the options each match takes.
split_pattern({re_pattern, Groups, _, _, _} = Pattern, Options, _) ->
    {Pattern, Groups, Options};
split_pattern(RE, Options, Arguments) when not is_tuple(RE) ->
    case compile(RE, [Option || Option <- Options, not is_run_option(Option)]) of
        {ok, {re_pattern, Groups, _, _, _} = Pattern} ->
            {Pattern, Groups, [Option || Option <- Options, not is_compile_option(Option)]};
        {error, _} ->
            erlang:error(badarg, Arguments)
    end;
split_pattern(_, _, Arguments) ->
    erlang:error(badarg, Arguments).

%% The parts before each match (and the match's groups), until Limit parts were cut.
cut(Subject, At, _, _, 0, Group) ->
    rest(Subject, At, Group);
cut(Subject, At, [], _, _, Group) ->
    rest(Subject, At, Group);
cut(Subject, At, _, _, _, Group) when byte_size(Subject) =< At ->
    wrapped(<<>>, Group);
cut(Subject, At, [[{Start, Length} | Groups] | Matches], Count, Limit, Group) ->
    Next = Start + Length,
    case Start =:= At andalso Length =:= 0 andalso lists:all(fun({_, L}) -> L =:= 0 end, Groups) of
        true ->
            cut(Subject, Next, Matches, Count, Limit, Group);
        false ->
            Kept = binary:part(Subject, At, Start - At),
            Values = [group(Subject, G) || G <- padded(Groups, Count)],
            Tail = cut(Subject, Next, Matches, Count, Limit - 1, Group),
            case Group of
                false -> [Kept | Values ++ Tail];
                true -> [[Kept | Values] | Tail]
            end
    end.

rest(Subject, At, Group) -> wrapped(binary:part(Subject, At, byte_size(Subject) - At), Group).

wrapped(Part, false) -> [Part];
wrapped(Part, true) -> [[Part]].

%% A match's groups, unset trailing ones as {0, 0}.
padded(_, 0) -> [];
padded([], N) -> [{0, 0} | padded([], N - 1)];
padded([G | Gs], N) -> [G | padded(Gs, N - 1)].

%% With trim, the trailing empty parts dropped (a group only when all of it is empty).
trimmed(Parts, false, _) -> Parts;
trimmed(Parts, true, false) -> drop_empty(Parts);
trimmed(Parts, true, true) -> drop_empty_groups(Parts).

drop_empty(Parts) ->
    lists:reverse(lists:dropwhile(fun(Part) -> Part =:= <<>> end, lists:reverse(Parts))).

drop_empty_groups(Groups) ->
    lists:reverse(
        lists:dropwhile(fun(Group) -> drop_empty(Group) =:= [] end, lists:reverse(Groups))
    ).

split_result(Parts, Return, Unicode, true) ->
    [split_values(Part, Return, Unicode) || Part <- Parts];
split_result(Parts, Return, Unicode, false) ->
    split_values(Parts, Return, Unicode).

split_values(Parts, list, true) -> [unicode:characters_to_list(Part, unicode) || Part <- Parts];
split_values(Parts, list, false) -> [binary_to_list(Part) || Part <- Parts];
split_values(Parts, _, _) -> Parts.

%% --- Builtins ---

%% Replaced at registration by the PCRE2 builtins (docs/builtins.md#re-module).
inspect(_, _) -> clause_builtin_placeholder.
version() -> clause_builtin_placeholder.
