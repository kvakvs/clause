-module(edlin_expand).
-export([expand/1, expand/2]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    bif/1,
    expand/3,
    format_matches/2,
    get_exports/1,
    is_type/3,
    match_arguments1/3,
    number_matches/1,
    over_word/1,
    shell_default_or_bif/1
]).

% Completion of the shell's input (plan 21 step LIB74B, docs/libraries/library.md#interactive-shell), written from OTP's
% documentation and observed results: the text before the cursor (reversed) is completed as a module, function,
% shell binding, record name or field, map key, fun arity or file path. Linked code has no type specifications or
% documentation, so a call's arguments are suggested as the function's arities.

-define(KEYWORDS, [
    "begin", "case", "of", "receive", "after", "maybe", "try", "catch", "throw", "if", "fun", "when", "end"
]).

%% Complete the reversed text before the cursor: {yes | no, Completion, Matches}, matches as {Name, ""}.
expand(Bef) ->
    expand(Bef, [{legacy_output, true}]).

%% The same with options: {legacy_output, false} gives matches in sections (#{title, elems, options}).
expand(Bef, Options) ->
    State =
        try
            shell:get_state()
        catch
            _:_ -> {shell_state, [], [], []}
        end,
    expand(Bef, Options, State).

expand(Bef, Options, {shell_state, Bs, RT, FT}) ->
    {Res, Completion, Matches0} = complete(Bef, Bs, RT, FT),
    Matches =
        case {Res, number_matches(Matches0)} of
            {yes, 1} -> [];
            _ -> Matches0
        end,
    case proplists:get_value(legacy_output, Options, false) of
        true -> {Res, Completion, legacy(Matches)};
        false -> {Res, Completion, Matches}
    end.

%% --- What the cursor is in ---

complete(Bef, Bs, RT, FT) ->
    {Before, Word} = over_word(Bef),
    case odd_quotes(Bef) of
        true -> expand_string(Bef);
        false -> complete_word(Before, Word, Bs, RT, FT)
    end.

complete_word([$# | Before], [], Bs, RT, _FT) ->
    map_or_record(Before, Bs, RT);
complete_word([$# | _], Word, _Bs, RT, _FT) ->
    expand_record(Word, RT);
complete_word(Before, [C | _] = Word, Bs, _RT, _FT) when C >= $A, C =< $Z; C =:= $_ ->
    _ = Before,
    expand_binding(Word, Bs);
complete_word(Before, Word, Bs, RT, FT) ->
    case enclosing(Before) of
        {${, [$# | Outside], Inside} -> map_keys(Outside, Inside, Word, Bs);
        {${, Outside, Inside} -> record_fields(Outside, Inside, Word, RT, FT);
        _ -> after_word(skip_white(Before), Word, FT)
    end.

% What precedes a word decides its completion: Module:Fun, fun Module:Fun/Arity, a call's arguments, or a term.
after_word([$: | Rest], Word, FT) ->
    {Before, Mod} = over_word(Rest),
    case over_word(Before) of
        {_, "fun"} -> expand_function_name(Mod, Word, "/", FT);
        _ when Mod =:= [] -> {no, [], []};
        _ -> expand_function_name(Mod, Word, "(", FT)
    end;
after_word([$/ | Rest], Word, FT) ->
    {Before, Fun} = over_word(Rest),
    case skip_white(Before) of
        [$: | Rest1] when Fun =/= [] ->
            {_, Mod} = over_word(Rest1),
            match(Word, [integer_to_list(A) || A <- arities(Mod, Fun, FT)], "");
        _ ->
            {no, [], []}
    end;
after_word(Before, [], FT) ->
    case enclosing(Before) of
        {$(, Outside, Inside} -> call_arguments(Outside, Inside, FT);
        _ -> {no, [], []}
    end;
after_word([C, Escape | _], _Word, _FT) when
    (C =:= $] orelse C =:= $) orelse C =:= $> orelse C =:= $} orelse C =:= $" orelse C =:= $'),
    Escape =/= $$,
    Escape =/= $-
->
    {no, [], []};
after_word(Before, Word, FT) ->
    case help_call(Before) of
        {true, CompleteChar} -> fold_results([expand_module_name(Word, CompleteChar)]);
        false when Before =:= [] -> fold_results(term_alternatives(FT, all, Word, ":"));
        false -> fold_results(term_alternatives(FT, other, Word, ":"))
    end.

% A word that is the first argument of h( or ht(: a module name, ", " after it.
help_call([$( | Rest]) ->
    case over_word(Rest) of
        {_, "h"} -> {true, ", "};
        {_, "ht"} -> {true, ", "};
        _ -> false
    end;
help_call(_) ->
    false.

term_alternatives(FT, all, Word, CompleteChar) ->
    [
        expand_module_name(Word, CompleteChar),
        expand_bifs(Word),
        expand_shell_default(Word),
        expand_user_defined(FT, Word),
        expand_keyword(Word)
    ];
term_alternatives(FT, other, Word, CompleteChar) ->
    [expand_module_name(Word, CompleteChar), expand_bifs(Word), expand_user_defined(FT, Word), expand_keyword(Word)].

% The innermost bracket open before the cursor: {Bracket, TextBeforeIt, TextAfterIt (both reversed)}, or none.
enclosing(Bef) -> enclosing(Bef, [], []).

enclosing([], _, _) ->
    none;
enclosing([C | Rest], [], Inside) when C =:= $(; C =:= ${; C =:= $[ ->
    {C, Rest, lists:reverse(Inside)};
enclosing([C | Rest], [C | Depth], Inside) when C =:= $(; C =:= ${; C =:= $[ ->
    enclosing(Rest, Depth, [C | Inside]);
enclosing([C | Rest], Depth, Inside) when C =:= $); C =:= $}; C =:= $] ->
    enclosing(Rest, [opening(C) | Depth], [C | Inside]);
enclosing([C | Rest], Depth, Inside) ->
    enclosing(Rest, Depth, [C | Inside]).

opening($)) -> $(;
opening($}) -> ${;
opening($]) -> $[.

%% --- Bindings, records and maps ---

expand_binding(Prefix, Bindings) ->
    Alts = [[C || C <- atom_to_list(K), C =/= $'] || {K, _} <- Bindings, is_atom(K)],
    section(match(Prefix, Alts, ""), "bindings").

expand_record(Prefix, RT) ->
    section(match(Prefix, [Name || {Name, _} <- RT, is_atom(Name)], "{"), "records").

% After Var#: a map binding opens its braces, a record binding names its record, else any record name.
map_or_record(Before, Bs, RT) ->
    {_, Var} = over_word(Before),
    case Var =/= [] andalso proplists:get_value(list_to_atom(Var), Bs) of
        Map when is_map(Map) ->
            {yes, "{", []};
        Tuple when is_tuple(Tuple), tuple_size(Tuple) > 0 ->
            Name = element(1, Tuple),
            case is_atom(Name) andalso lists:keymember(Name, 1, RT) of
                true -> match([], [Name], "{");
                false -> {no, [], []}
            end;
        _ ->
            expand_record([], RT)
    end.

% Inside Var#{...: the atom keys of the map binding not given yet.
map_keys(Outside, Inside, Word, Bs) ->
    {_, Var} = over_word(Outside),
    case Var =/= [] andalso proplists:get_value(list_to_atom(Var), Bs) of
        Map when is_map(Map) ->
            Given = given_keys(Inside),
            match(Word, [K || K <- maps:keys(Map), is_atom(K), not lists:member(atom_to_list(K), Given)], "=>");
        _ ->
            {no, [], []}
    end.

% Inside #Record{...: the record's fields (OTP offers those given already too); inside other braces, a term.
record_fields(Outside, Inside, Word, RT, FT) ->
    case over_word(Outside) of
        {[$# | _], Record} when Record =/= [] ->
            case lists:keyfind(list_to_atom(Record), 1, RT) of
                {_, {attribute, _, record, {_, Fields}}} ->
                    section(match(Word, field_names(Fields), "="), "fields");
                _ ->
                    {no, [], []}
            end;
        _ ->
            after_word(skip_white(Inside), Word, FT)
    end.

% The keys or fields given so far: the words before = or => at the top level of the braces.
given_keys(Inside) ->
    Elements = string:split(lists:reverse(Inside), ",", all),
    [
        string:trim(Key)
     || Element <- Elements, [Key | _] <- [string:split(Element, "=")], string:find(Element, "=") =/= nomatch
    ].

field_names(Fields) ->
    [field_name(F) || F <- Fields].

field_name({typed_record_field, Field, _}) -> field_name(Field);
field_name({record_field, _, {atom, _, Name}}) -> Name;
field_name({record_field, _, {atom, _, Name}, _}) -> Name.

%% --- Modules and functions ---

% Inside Mod:Fun(: the function's arities as heads, Mod:Fun/Arity, linked code having no specifications.
call_arguments(Outside, Inside, FT) ->
    {Before, Fun} = over_word(Outside),
    case skip_white(Before) of
        _ when Fun =:= [] ->
            {no, [], []};
        [$: | Rest] ->
            {_, Mod} = over_word(Rest),
            function_heads(Mod, Fun, argument_count(Inside), FT);
        _ ->
            function_heads(shell_or_erlang(Fun), Fun, argument_count(Inside), FT)
    end.

shell_or_erlang(Fun) ->
    case shell_default_or_bif(Fun) of
        "erlang" -> "erlang";
        _ -> "shell_default"
    end.

argument_count(Inside) ->
    length([C || C <- Inside, C =:= $,]).

function_heads(Mod, Fun, Given, FT) ->
    Heads = [
        #{title => Mod ++ ":" ++ Fun ++ "/" ++ integer_to_list(A), elems => [], options => []}
     || A <- arities(Mod, Fun, FT), A >= Given
    ],
    case Heads of
        [] -> {no, [], []};
        _ -> {no, [], [#{title => "typespecs", elems => Heads, options => [highlight_all]}]}
    end.

% The arities Mod exports Fun with; shell_default's include the functions defined in the shell.
arities("shell_default", Fun, FT) ->
    case [A || {{function, {_, F, A}}, _} <- FT, atom_to_list(F) =:= Fun] of
        [] -> arities("shell_default", Fun);
        Arities -> Arities
    end;
arities(Mod, Fun, _FT) ->
    arities(Mod, Fun).

arities(Mod, Fun) ->
    case to_atom(Mod) of
        {ok, Module} -> lists:sort([A || {F, A} <- get_exports(Module), flat_write(F) =:= Fun]);
        error -> []
    end.

expand_function_name(ModText, Prefix, CompleteChar, FT) ->
    case to_atom(ModText) of
        {ok, shell_default} ->
            Defined = [{Name, Arity} || {{function, {_, Name, Arity}}, _} <- FT],
            section(match(Prefix, Defined, CompleteChar), "Shell defined functions");
        {ok, Mod} ->
            section(match(Prefix, get_exports(Mod), CompleteChar), "Functions");
        error ->
            {no, [], []}
    end.

expand_module_name("", _) ->
    {no, [], []};
expand_module_name(Prefix, CompleteChar) ->
    Alts = [{list_to_atom(M), ""} || {M, _, _} <- code:all_available()],
    section(match(Prefix, Alts, CompleteChar), "modules").

expand_bifs(Prefix) ->
    section(match(Prefix, [FA || {F, A} = FA <- get_exports(erlang), erl_internal:bif(F, A)], "("), "bifs").

expand_shell_default(Prefix) ->
    section(match(Prefix, get_exports(shell_default) ++ shell:local_func(), "("), "commands").

expand_user_defined(FT, Prefix) ->
    section(match(Prefix, [{Name, Arity} || {{function, {_, Name, Arity}}, _} <- FT], "("), "user_defined").

expand_keyword(Word) ->
    case match(Word, ?KEYWORDS, "") of
        {_, _, []} -> {no, [], []};
        {_, _, [{Word, _}]} -> {no, [], []};
        Result -> section(Result, "keywords")
    end.

%% --- Files ---

% Inside a string: the entries of the directory it names, hidden ones only for a word starting with a dot.
expand_string(Bef) ->
    case file_path(Bef, []) of
        none ->
            {no, [], []};
        Path ->
            {Dir, Word} = split_at_last_slash(Path),
            expand_file(Dir, Word)
    end.

file_path([], _) -> none;
file_path([$", $\\ | Bef], Path) -> file_path(Bef, [$" | Path]);
file_path([$" | _], Path) -> Path;
file_path([$\s, $\\ | Bef], Path) -> file_path(Bef, [$\s | Path]);
file_path([$\s | _], _) -> none;
file_path([C | Bef], Path) -> file_path(Bef, [C | Path]).

split_at_last_slash(Path) ->
    {File, Dir} = lists:splitwith(fun(C) -> C =/= $/ end, lists:reverse(Path)),
    {lists:reverse(Dir), lists:reverse(File)}.

expand_file(Prefix, Word) ->
    Dir =
        case Prefix of
            [$/ | _] ->
                Prefix;
            _ ->
                {ok, Cwd} = file:get_cwd(),
                Cwd ++ "/" ++ Prefix
        end,
    Hidden = lists:prefix(".", Word),
    Entries =
        case file:list_dir(Dir) of
            {ok, Names} -> [entry(Dir, Name) || Name <- [".." | Names]];
            _ -> []
        end,
    Shown = [E || E <- Entries, Hidden orelse not lists:prefix(".", E)],
    case match(Word, Shown, []) of
        {yes, Completion, [Match]} ->
            case filelib:is_dir(Dir ++ "/" ++ Word ++ Completion) of
                true -> {yes, Completion, [Match]};
                false -> {yes, Completion ++ "\"", [Match]}
            end;
        Other ->
            Other
    end.

entry(Dir, Name) ->
    case filelib:is_dir(Dir ++ "/" ++ Name) of
        true -> Name ++ "/";
        false -> Name
    end.

%% --- Matching ---

% The alternatives starting with Prefix: {yes, Completion, Matches} when they share more than the prefix (the
% ending appended for a single one), else {no, [], Matches}. An alternative is a name or {Name, Arity}.
match(Prefix, Alts, Ending) ->
    Named = lists:sort([{flat_write(H), A} || {H, A} <- [alternative(Alt) || Alt <- Alts]]),
    Matching = [{S, A} || {S, A} <- Named, lists:prefix(Prefix, S)],
    Elems = lists:usort([{S, endings(Ending)} || {S, _} <- Matching]),
    case [S || {S, _} <- Matching] of
        [] ->
            {no, [], []};
        Names ->
            case common_prefix(Names) of
                {all, Name} ->
                    {yes, string:slice(Name, string:length(Prefix)) ++ ending(Ending, Name, Matching), Elems};
                {some, []} ->
                    {no, [], Elems};
                {some, Common} ->
                    {yes, string:slice(Common, string:length(Prefix)), Elems}
            end
    end.

alternative({_, _} = Alt) -> Alt;
alternative(Name) -> {Name, ""}.

endings([]) -> [];
endings(Ending) -> [{ending, Ending}].

% The ending of a single completed name: its arity after /, () for a function of arity 0.
ending("/", Name, [{Name, N}]) when is_integer(N) -> "/" ++ integer_to_list(N);
ending("(", Name, [{Name, 0}]) -> "()";
ending(Ending, _, _) -> Ending.

% {all, Name} when every name is the same, else {some, Prefix} with their longest common prefix.
common_prefix([First | Rest]) ->
    case lists:all(fun(N) -> N =:= First end, Rest) of
        true -> {all, First};
        false -> {some, lists:foldl(fun shared/2, First, Rest)}
    end.

shared(A, B) -> shared(A, B, []).

shared([C | A], [C | B], Acc) -> shared(A, B, [C | Acc]);
shared(_, _, Acc) -> lists:reverse(Acc).

% A result's matches as a section with a title.
section({_, _, []} = Result, _Title) ->
    Result;
section({Res, Completion, Matches}, Title) ->
    {Res, Completion, [#{title => Title, elems => Matches, options => [highlight_all]}]}.

% Combine the results of several completions: a common completion stays, the matches are merged.
fold_results([]) -> {no, [], []};
fold_results([R | Results]) -> lists:foldl(fun fold/2, R, Results).

fold({yes, C1, M1}, {yes, C2, M2}) ->
    case shared(C1, C2) of
        [] -> {no, [], ordsets:union(M1, M2)};
        C -> {yes, C, ordsets:union(M1, M2)}
    end;
fold({yes, C, M}, {no, [], []}) ->
    {yes, C, M};
fold({no, [], []}, {yes, C, M}) ->
    {yes, C, M};
fold({_, _, M1}, {_, _, M2}) ->
    {no, [], ordsets:union(M1, M2)}.

%% The number of matches in a result's matches, counting through sections.
number_matches([#{elems := Matches} | T]) -> number_matches(Matches) + number_matches(T);
number_matches([_ | T]) -> 1 + number_matches(T);
number_matches([]) -> 0.

% Sections as OTP's legacy matches: the elements of the plain sections, the titles of the others; commands and
% BIFs left out.
legacy([]) ->
    [];
legacy([#{title := Title} | Rest]) when Title =:= "commands"; Title =:= "bifs" ->
    legacy(Rest);
legacy([#{title := Title, elems := Elems} | Rest]) when
    Title =:= "modules";
    Title =:= "functions";
    Title =:= "bindings";
    Title =:= "records";
    Title =:= "fields";
    Title =:= "types";
    Title =:= "atoms";
    Title =:= "matches";
    Title =:= "keywords";
    Title =:= "typespecs"
->
    legacy(Elems) ++ legacy(Rest);
legacy([#{title := Title} | Rest]) ->
    [Title | legacy(Rest)];
legacy([{Value, _} | Rest]) ->
    [{Value, ""} | legacy(Rest)].

%% --- Words ---

%% The text before a word and the word (in reading order), after skipping blanks before the cursor.
over_word(Bef) ->
    over_word(skip_white(Bef), []).

over_word([C | Bef], Word) when
    C >= $a, C =< $z; C >= $A, C =< $Z; C >= $0, C =< $9; C =:= $_; C =:= $@; C >= 192, C =/= 215, C =/= 247
->
    over_word(Bef, [C | Word]);
over_word([$' | Bef], []) ->
    quoted(Bef, [$']);
over_word(Bef, Word) ->
    {Bef, Word}.

% A quoted atom read backwards up to its opening quote.
quoted([$' | Bef], Word) -> {Bef, [$' | Word]};
quoted([C | Bef], Word) -> quoted(Bef, [C | Word]);
quoted([], Word) -> {[], Word}.

skip_white([C | Bef]) when C =:= $\s; C =:= $\t -> skip_white(Bef);
skip_white(Bef) -> Bef.

% Whether the cursor is in a string: an odd number of double quotes before it, escaped ones and $" not counted.
odd_quotes(Bef) -> odd_quotes(lists:reverse(Bef), false).

odd_quotes([$$, $" | Rest], Odd) -> odd_quotes(Rest, Odd);
odd_quotes([$\\, $" | Rest], Odd) -> odd_quotes(Rest, Odd);
odd_quotes([$" | Rest], Odd) -> odd_quotes(Rest, not Odd);
odd_quotes([_ | Rest], Odd) -> odd_quotes(Rest, Odd);
odd_quotes([], Odd) -> Odd.

to_atom(Text) ->
    case erl_scan:string(Text) of
        {ok, [{atom, _, A}], _} -> {ok, A};
        _ -> error
    end.

flat_write(T) when is_atom(T) -> lists:flatten(io_lib:fwrite("~tw", [T]));
flat_write(S) -> S.

format_matches(_, _) -> erlang:error({not_implemented, format_matches, 2}).
is_type(_, _, _) -> erlang:error({not_implemented, is_type, 3}).
match_arguments1(_, _, _) -> erlang:error({not_implemented, match_arguments1, 3}).

%% Where a local call of Fun goes: "shell_default", "erlang", "shell" (a shell command) or "user_defined".
shell_default_or_bif(Fun) when is_atom(Fun) ->
    case lists:keymember(Fun, 1, get_exports(shell_default)) of
        true -> "shell_default";
        false -> bif(Fun)
    end;
shell_default_or_bif(Fun) ->
    case erl_scan:string(Fun) of
        {ok, [{atom, _, Name}], _} -> shell_default_or_bif(Name);
        _ -> []
    end.

%% "erlang" for a function erlang exports, else as a shell command or a user's function.
bif(Fun) when is_atom(Fun) ->
    case lists:keymember(Fun, 1, get_exports(erlang)) of
        true ->
            "erlang";
        false ->
            case shell:local_func(Fun) of
                true -> "shell";
                false -> "user_defined"
            end
    end;
bif(Fun) ->
    case erl_scan:string(Fun) of
        {ok, [{atom, _, Name}], _} -> bif(Name);
        _ -> []
    end.

%% The exports of a loaded module, or of its object file.
get_exports(Mod) ->
    case erlang:module_loaded(Mod) of
        true ->
            Mod:module_info(exports);
        false ->
            case beam_lib:chunks(code:which(Mod), [exports]) of
                {ok, {Mod, [{exports, E}]}} -> E;
                _ -> []
            end
    end.
