%% Clause's erl_syntax_lib (plan 21 LIB77E4a, docs/libraries/syntax_tools.md#erl_syntax_lib): utilities over
%% erl_syntax trees. The folds and maps take funs and stay Erlang, as do the variable names, which draw on rand, and
%% to_comment/1,2,3; the analyses, binding annotations and the walks without funs are runtime builtins
%% (runtime/src/builtins/syntax_lib/) with clause_builtin_placeholder bodies.
-module(erl_syntax_lib).
-export([
    analyze_application/1,
    analyze_attribute/1,
    analyze_export_attribute/1,
    analyze_file_attribute/1,
    analyze_form/1,
    analyze_forms/1,
    analyze_function/1,
    analyze_function_name/1,
    analyze_implicit_fun/1,
    analyze_import_attribute/1,
    analyze_module_attribute/1,
    analyze_record_attribute/1,
    analyze_record_expr/1,
    analyze_record_field/1,
    analyze_type_application/1,
    analyze_type_name/1,
    analyze_wild_attribute/1,
    annotate_bindings/1,
    annotate_bindings/2,
    fold/3,
    fold_subtrees/3,
    foldl_listlist/3,
    function_name_expansions/1,
    is_fail_expr/1,
    limit/2,
    limit/3,
    map/2,
    map_subtrees/2,
    mapfold/3,
    mapfold_subtrees/3,
    mapfoldl_listlist/3,
    new_variable_name/1,
    new_variable_name/2,
    new_variable_names/2,
    new_variable_names/3,
    strip_comments/1,
    to_comment/1,
    to_comment/2,
    to_comment/3,
    variables/1
]).
-include("erl_syntax_lib.types.hrl").

%% Folds and maps: children before their node, groups and subtrees left to right.

map(F, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] -> F(Tree);
        Groups -> F(rebuild(Tree, [[map(F, T) || T <- Group] || Group <- Groups]))
    end.

map_subtrees(F, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] -> Tree;
        Groups -> rebuild(Tree, [[F(T) || T <- Group] || Group <- Groups])
    end.

fold(F, S, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] -> F(Tree, S);
        Groups -> F(Tree, fold_groups(F, S, Groups))
    end.

fold_groups(F, S, [Group | Groups]) -> fold_groups(F, fold_group(F, S, Group), Groups);
fold_groups(_, S, []) -> S.

fold_group(F, S, [T | Ts]) -> fold_group(F, fold(F, S, T), Ts);
fold_group(_, S, []) -> S.

fold_subtrees(F, S, Tree) -> foldl_listlist(F, S, erl_syntax:subtrees(Tree)).

foldl_listlist(F, S, [L | Ls]) -> foldl_listlist(F, foldl(F, S, L), Ls);
foldl_listlist(_, S, []) -> S.

foldl(F, S, [T | Ts]) -> foldl(F, F(T, S), Ts);
foldl(_, S, []) -> S.

mapfold(F, S, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] ->
            F(Tree, S);
        Groups ->
            {Groups1, S1} = mapfold_groups(F, S, Groups),
            F(rebuild(Tree, Groups1), S1)
    end.

mapfold_groups(F, S, [Group | Groups]) ->
    {Group1, S1} = mapfold_group(F, S, Group),
    {Groups1, S2} = mapfold_groups(F, S1, Groups),
    {[Group1 | Groups1], S2};
mapfold_groups(_, S, []) ->
    {[], S}.

mapfold_group(F, S, [T | Ts]) ->
    {T1, S1} = mapfold(F, S, T),
    {Ts1, S2} = mapfold_group(F, S1, Ts),
    {[T1 | Ts1], S2};
mapfold_group(_, S, []) ->
    {[], S}.

mapfold_subtrees(F, S, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] ->
            {Tree, S};
        Groups ->
            {Groups1, S1} = mapfoldl_listlist(F, S, Groups),
            {rebuild(Tree, Groups1), S1}
    end.

mapfoldl_listlist(F, S, [L | Ls]) ->
    {L1, S1} = mapfoldl(F, S, L),
    {Ls1, S2} = mapfoldl_listlist(F, S1, Ls),
    {[L1 | Ls1], S2};
mapfoldl_listlist(_, S, []) ->
    {[], S}.

mapfoldl(F, S, [X | Xs]) ->
    {X1, S1} = F(X, S),
    {Xs1, S2} = mapfoldl(F, S1, Xs),
    {[X1 | Xs1], S2};
mapfoldl(_, S, []) ->
    {[], S}.

% A node of Tree's type with new subtree groups and Tree's attributes.
rebuild(Tree, Groups) -> erl_syntax:copy_attrs(Tree, erl_syntax:make_tree(erl_syntax:type(Tree), Groups)).

%% Fresh variable names: random numbers in a range that grows with the set, enlarged eightfold after three misses.

new_variable_name(S) -> new_variable_name(fun default_variable_name/1, S).

new_variable_name(F, S) -> fresh_name(start_range(S), F, S).

new_variable_names(N, S) -> new_variable_names(N, fun default_variable_name/1, S).

new_variable_names(N, F, S) when is_integer(N) -> fresh_names(N, [], start_range(S), F, S).

fresh_names(N, Names, Range, F, S) when N > 0 ->
    Name = fresh_name(Range, F, S),
    fresh_names(N - 1, [Name | Names], Range, F, sets:add_element(Name, S));
fresh_names(0, Names, _, _, _) ->
    Names.

fresh_name(Range, F, S) -> fresh_name(random(Range), Range, 0, F, S).

fresh_name(N, Range, Tries, F, S) when Tries < 3 ->
    Name = F(N),
    case sets:is_element(Name, S) of
        true -> fresh_name(random(Range), Range, Tries + 1, F, S);
        false -> Name
    end;
fresh_name(_, Range, _, F, S) ->
    Larger = Range * 8,
    fresh_name(random(Larger), Larger, 0, F, S).

default_variable_name(N) -> list_to_atom("V" ++ integer_to_list(N)).

start_range(S) -> max(sets:size(S) * 100, 100).

% A number in 1..Range from the process's rand state, seeded as OTP seeds it when the process has none.
random(Range) ->
    _ =
        case rand:export_seed() of
            undefined -> rand:seed(exsplus, {753, 8, 73});
            _ -> ok
        end,
    rand:uniform(Range).

%% Comments of a tree's text.

to_comment(Tree) -> to_comment(Tree, "% ").

to_comment(Tree, Prefix) -> to_comment(Tree, Prefix, fun(T) -> erl_prettypr:format(T) end).

to_comment(Tree, Prefix, F) -> erl_syntax:comment(comment_lines(F(Tree), Prefix)).

% The lines of a text, each after Prefix: \r\n, \r and \n end lines, a tab pads to the next multiple of 8 columns
% (counted from the line start or the previous tab), and an empty last line is left out.
comment_lines(Text, Prefix) -> lists:reverse(comment_lines(Text, 0, [], [], Prefix)).

comment_lines([$\r, $\n | Text], _, Line, Lines, Prefix) ->
    next_line(Text, Line, Lines, Prefix);
comment_lines([$\r | Text], _, Line, Lines, Prefix) ->
    next_line(Text, Line, Lines, Prefix);
comment_lines([$\n | Text], _, Line, Lines, Prefix) ->
    next_line(Text, Line, Lines, Prefix);
comment_lines([$\t | Text], Column, Line, Lines, Prefix) ->
    comment_lines(Text, 0, lists:duplicate(8 - Column rem 8, $\s) ++ Line, Lines, Prefix);
comment_lines([C | Text], Column, Line, Lines, Prefix) ->
    comment_lines(Text, Column + 1, [C | Line], Lines, Prefix);
comment_lines([], _, [], Lines, _) ->
    Lines;
comment_lines([], _, Line, Lines, Prefix) ->
    [Prefix ++ lists:reverse(Line) | Lines].

next_line(Text, Line, Lines, Prefix) -> comment_lines(Text, 0, [], [Prefix ++ lists:reverse(Line) | Lines], Prefix).

% Native analyze_application/1.
analyze_application(_) -> clause_builtin_placeholder.

% Native analyze_attribute/1.
analyze_attribute(_) -> clause_builtin_placeholder.

% Native analyze_export_attribute/1.
analyze_export_attribute(_) -> clause_builtin_placeholder.

% Native analyze_file_attribute/1.
analyze_file_attribute(_) -> clause_builtin_placeholder.

% Native analyze_form/1.
analyze_form(_) -> clause_builtin_placeholder.

% Native analyze_forms/1.
analyze_forms(_) -> clause_builtin_placeholder.

% Native analyze_function/1.
analyze_function(_) -> clause_builtin_placeholder.

% Native analyze_function_name/1.
analyze_function_name(_) -> clause_builtin_placeholder.

% Native analyze_implicit_fun/1.
analyze_implicit_fun(_) -> clause_builtin_placeholder.

% Native analyze_import_attribute/1.
analyze_import_attribute(_) -> clause_builtin_placeholder.

% Native analyze_module_attribute/1.
analyze_module_attribute(_) -> clause_builtin_placeholder.

% Native analyze_record_attribute/1.
analyze_record_attribute(_) -> clause_builtin_placeholder.

% Native analyze_record_expr/1.
analyze_record_expr(_) -> clause_builtin_placeholder.

% Native analyze_record_field/1.
analyze_record_field(_) -> clause_builtin_placeholder.

% Native analyze_type_application/1.
analyze_type_application(_) -> clause_builtin_placeholder.

% Native analyze_type_name/1.
analyze_type_name(_) -> clause_builtin_placeholder.

% Native analyze_wild_attribute/1.
analyze_wild_attribute(_) -> clause_builtin_placeholder.

% Native annotate_bindings/1.
annotate_bindings(_) -> clause_builtin_placeholder.

% Native annotate_bindings/2.
annotate_bindings(_, _) -> clause_builtin_placeholder.

% Native function_name_expansions/1.
function_name_expansions(_) -> clause_builtin_placeholder.

% Native is_fail_expr/1.
is_fail_expr(_) -> clause_builtin_placeholder.

% Native limit/2.
limit(_, _) -> clause_builtin_placeholder.

% Native limit/3.
limit(_, _, _) -> clause_builtin_placeholder.

% Native strip_comments/1.
strip_comments(_) -> clause_builtin_placeholder.

% Native variables/1.
variables(_) -> clause_builtin_placeholder.
