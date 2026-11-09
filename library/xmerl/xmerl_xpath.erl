%% OTP's xmerl_xpath: XPath over scanned XML (plan 21 step MIM3b, docs/libraries/xmerl.md#xmerl_xpath), as OTP 29's
%% answers: its parse trees, each step applied to the context nodes in turn and the results joined, unions in term
%% order, an element's string value its own text children, equality on the first node of a node-set, and
%% primary expressions OTP does not evaluate left as {primary_expression, {not_implemented, Tree}}.
-module(xmerl_xpath).
-include("include/xmerl.hrl").
-export([string/2, string/3]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([axis/3, axis/4, eval_path/3, string/5, write_node/1]).

% A node reached by the evaluation: the record (or the root, {root, Document}) and its ancestors, nearest first.
-record(n, {node, parents = []}).

string(Text, Node) ->
    string(Text, Node, []).

string(Text, Node, Options) ->
    lists:foreach(fun option/1, Options),
    Context = context(Node),
    evaluate(clause_xmerl_xpath:parse(Text), Context).

option({namespace, _}) -> ok;
option({bindings, _}) -> ok;
option({functions, _}) -> ok;
option(Option) -> erlang:error(function_clause, [[Option]]).

% The context of a query: the node given, under a root whose child it is (or the document's root).
context(#xmlDocument{} = Doc) ->
    #n{node = {root, Doc}};
context(Node) when
    is_record(Node, xmlElement);
    is_record(Node, xmlText);
    is_record(Node, xmlAttribute);
    is_record(Node, xmlComment);
    is_record(Node, xmlPI)
->
    #n{node = Node, parents = []};
context(Other) ->
    erlang:error(function_clause, [Other]).

% A query's result: the nodes of a node-set, or the #xmlObj{} of a value.
evaluate({path, _, _} = Path, Context) ->
    [record(N) || N <- path_nodes(Path, Context)];
evaluate({function_call, _, _} = Call, Context) ->
    object(call(Call, Context, 1, 0));
evaluate({literal, String}, _) ->
    #xmlObj{type = string, value = String};
evaluate({number, Number}, _) ->
    #xmlObj{type = number, value = Number};
evaluate(Expr, _) ->
    exit({primary_expression, {not_implemented, Expr}}).

object({nodeset, Nodes}) -> [record(N) || N <- Nodes];
object({Type, Value}) -> #xmlObj{type = Type, value = Value}.

record(#n{node = {root, Doc}}) -> Doc;
record(#n{node = Node}) -> Node.

%% --- Paths ---

path_nodes({path, abs, '/'}, Context) ->
    [root(Context)];
path_nodes({path, abs, Steps}, Context) ->
    steps(Steps, [root(Context)]);
path_nodes({path, rel, Steps}, Context) ->
    steps(Steps, [Context]);
path_nodes({path, union, {Left, Right}}, Context) ->
    union(path_nodes(Left, Context) ++ path_nodes(Right, Context));
path_nodes({path, filter, {Expr, {pred, Pred}}}, Context) ->
    predicate(Pred, nodeset(value(Expr, Context, 1, 1)));
path_nodes(Expr, Context) ->
    nodeset(value(Expr, Context, 1, 1)).

% The root above a node: its document, or a document whose content is the node at the top of its ancestors.
root(#n{node = {root, _}} = Root) ->
    Root;
root(#n{parents = []} = N) ->
    #n{node = {root, #xmlDocument{content = N#n.node}}};
root(#n{parents = Parents}) ->
    lists:last(Parents).

% A union: its nodes once each, in the order of their records (OTP sorts them).
union(Nodes) ->
    Keyed = lists:ukeysort(1, [{{kind(N#n.node), N#n.node}, N} || N <- Nodes]),
    [N || {_, N} <- Keyed].

kind(#xmlElement{}) -> element;
kind(#xmlAttribute{}) -> attribute;
kind(#xmlText{}) -> text;
kind(#xmlComment{}) -> comment;
kind(#xmlPI{}) -> processing_instruction;
kind(_) -> root_node.

steps({refine, Left, Right}, Contexts) ->
    steps(Right, steps(Left, Contexts));
steps({step, {Axis, Test, Preds}}, Contexts) ->
    Candidates = lists:append([step(Axis, Test, Preds, C) || C <- Contexts]),
    lists:foldl(fun({pred, Pred}, Acc) -> predicate(Pred, Acc) end, Candidates, Preds).

% One step from one context node: the axis's nodes passing the test.
step(Axis, Test, Preds, Context) ->
    ancestor_predicates(Axis, Preds, Context),
    [N || N <- axis_nodes(Axis, Context), test(Test, Axis, N#n.node)].

% OTP's ancestor steps with predicates fail.
ancestor_predicates(Axis, [_ | _], #n{parents = [#n{node = #xmlElement{name = Name}} | _]}) when
    Axis =:= ancestor; Axis =:= ancestor_or_self
->
    erlang:error({badarg, Name});
ancestor_predicates(_, _, _) ->
    ok.

%% --- Axes, in document order ---

axis_nodes(child, N) ->
    children(N);
axis_nodes(descendant, N) ->
    descendants(N);
axis_nodes(descendant_or_self, N) ->
    [N | descendants(N)];
axis_nodes(self, N) ->
    [N];
axis_nodes(parent, #n{parents = [P | _]}) ->
    [P];
axis_nodes(parent, _) ->
    [];
axis_nodes(ancestor, #n{parents = Parents}) ->
    lists:reverse(elements(Parents));
axis_nodes(ancestor_or_self, #n{parents = Parents} = N) ->
    lists:reverse([N | elements(Parents)]);
axis_nodes(attribute, #n{node = #xmlElement{attributes = As}, parents = Ps} = N) ->
    [#n{node = A, parents = [N | Ps]} || A <- As, not declaration(A#xmlAttribute.name)];
axis_nodes(attribute, _) ->
    [];
axis_nodes(following_sibling, N) ->
    element(2, siblings(N));
axis_nodes(preceding_sibling, N) ->
    element(1, siblings(N));
axis_nodes(following, N) ->
    following(N);
axis_nodes(preceding, N) ->
    preceding(N);
axis_nodes(namespace, _) ->
    [].

elements(Nodes) -> [N || #n{node = #xmlElement{}} = N <- Nodes].

declaration(xmlns) -> true;
declaration(Name) -> lists:prefix("xmlns:", atom_to_list(Name)).

children(#n{node = {root, #xmlDocument{content = Content}}} = N) when is_list(Content) ->
    [#n{node = C, parents = [N]} || C <- Content];
children(#n{node = {root, #xmlDocument{content = Content}}} = N) ->
    [#n{node = Content, parents = [N]}];
children(#n{node = #xmlElement{content = Content}, parents = Ps} = N) ->
    [#n{node = C, parents = [N | Ps]} || C <- Content];
children(_) ->
    [].

descendants(N) ->
    lists:append([[C | descendants(C)] || C <- children(N)]).

% The siblings before and after a node, each in document order.
siblings(#n{node = #xmlAttribute{}}) ->
    {[], []};
siblings(#n{node = Node, parents = [P | _]}) ->
    Siblings = children(P),
    {Before, [_ | After]} = lists:splitwith(fun(S) -> S#n.node =/= Node end, Siblings),
    {Before, After};
siblings(_) ->
    {[], []}.

following(#n{parents = []}) ->
    [];
following(#n{parents = [_ | _]} = N) ->
    {_, After} = siblings(N),
    [Node || S <- After, Node <- [S | descendants(S)]] ++ following(hd(N#n.parents)).

preceding(#n{parents = []}) ->
    [];
preceding(#n{parents = [_ | _]} = N) ->
    {Before, _} = siblings(N),
    preceding(hd(N#n.parents)) ++ [Node || S <- Before, Node <- [S | descendants(S)]].

%% --- Node tests ---

test({wildcard, wildcard}, attribute, #xmlAttribute{}) -> true;
test({wildcard, wildcard}, Axis, #xmlElement{}) when Axis =/= attribute -> true;
test({name, {Name, _, _}}, attribute, #xmlAttribute{name = Name}) -> true;
test({name, {Name, _, _}}, Axis, #xmlElement{name = Name}) when Axis =/= attribute -> true;
test({prefix_test, Prefix}, attribute, #xmlAttribute{nsinfo = {Prefix, _}}) -> true;
test({prefix_test, Prefix}, Axis, #xmlElement{nsinfo = {Prefix, _}}) when Axis =/= attribute -> true;
test({node_type, node}, _, {root, _}) -> true;
test({node_type, node}, attribute, #xmlAttribute{}) -> true;
test({node_type, node}, Axis, Node) when Axis =/= attribute -> not is_record(Node, xmlAttribute);
test({node_type, text}, _, #xmlText{}) -> true;
test({node_type, comment}, _, #xmlComment{}) -> true;
test({processing_instruction, Name}, _, #xmlPI{name = Target}) -> atom_to_list(Target) =:= Name;
test(_, _, _) -> false.

%% --- Predicates ---

% A number picks that position (none past the last, OTP's lists:nth error before the first); another value keeps
% the nodes for which it holds, a number where it equals the position.
predicate({number, N}, Nodes) when is_integer(N), N > length(Nodes) ->
    [];
predicate({number, N}, Nodes) when is_integer(N), N >= 1 ->
    [lists:nth(N, Nodes)];
predicate({number, N}, Nodes) ->
    erlang:error(function_clause, [N, Nodes]);
predicate(Pred, Nodes) ->
    Size = length(Nodes),
    Indexed = lists:zip(lists:seq(1, Size), Nodes),
    [N || {Pos, N} <- Indexed, holds(value(Pred, N, Pos, Size), Pos)].

holds({number, N}, Pos) -> N == Pos;
holds(Value, _) -> boolean(Value).

%% --- Values: {nodeset, Nodes}, {string, S}, {number, N} or {boolean, B} ---

value({path, _, _} = Path, Context, _, _) ->
    {nodeset, path_nodes(Path, Context)};
value({literal, String}, _, _, _) ->
    {string, String};
value({number, Number}, _, _, _) ->
    {number, Number};
value({function_call, _, _} = Call, Context, Pos, Size) ->
    call(Call, Context, Pos, Size);
value({bool, 'and', L, R}, Context, Pos, Size) ->
    {boolean, boolean(value(L, Context, Pos, Size)) andalso boolean(value(R, Context, Pos, Size))};
value({bool, 'or', L, R}, Context, Pos, Size) ->
    {boolean, boolean(value(L, Context, Pos, Size)) orelse boolean(value(R, Context, Pos, Size))};
value({comp, '=', L, R}, Context, Pos, Size) ->
    {boolean, equal(value(L, Context, Pos, Size), value(R, Context, Pos, Size))};
value({comp, '!=', L, R}, Context, Pos, Size) ->
    {boolean, not equal(value(L, Context, Pos, Size), value(R, Context, Pos, Size))};
value({comp, _, L, R}, Context, Pos, Size) ->
    % OTP's relational operators all compare as greater than, the values as Erlang terms.
    {boolean, relational(value(L, Context, Pos, Size)) > relational(value(R, Context, Pos, Size))};
value(Expr, _, _, _) ->
    exit({primary_expression, {not_implemented, Expr}}).

nodeset({nodeset, Nodes}) -> Nodes;
nodeset(Value) -> erlang:error({badarg, Value}).

boolean({nodeset, Nodes}) -> Nodes =/= [];
boolean({string, S}) -> S =/= [];
boolean({number, N}) -> N /= 0;
boolean({boolean, B}) -> B.

% Equality: booleans as booleans, then numbers as numbers, else strings; a node-set by its first node.
equal({boolean, _} = A, B) -> boolean(A) =:= boolean(B);
equal(A, {boolean, _} = B) -> boolean(A) =:= boolean(B);
equal({number, _} = A, B) -> number(A) == number(B);
equal(A, {number, _} = B) -> number(A) == number(B);
equal(A, B) -> string(A) =:= string(B).

relational({nodeset, []}) -> [];
relational(Value) -> number(Value).

%% --- Functions ---

call({function_call, Name, Args}, Context, Pos, Size) ->
    Values = [value(A, Context, Pos, Size) || A <- Args],
    function(Name, Values, Context, Pos, Size).

function(last, [], _, _, Size) ->
    {number, Size};
function(position, [], _, Pos, _) ->
    {number, Pos};
function(count, [{nodeset, Nodes}], _, _, _) ->
    {number, length(Nodes)};
function(name, [], Context, _, _) ->
    {string, name(Context)};
function(name, [{nodeset, Nodes}], _, _, _) ->
    {string, first(fun name/1, Nodes)};
function('local-name', [], Context, _, _) ->
    {string, local_name(Context)};
function('local-name', [{nodeset, Nodes}], _, _, _) ->
    {string, first(fun local_name/1, Nodes)};
function('namespace-uri', [{nodeset, Nodes}], _, _, _) ->
    {string, first(fun namespace_uri/1, Nodes)};
function(string, [], Context, _, _) ->
    {string, string_value(Context)};
function(string, [Value], _, _, _) ->
    {string, string(Value)};
function(concat, Values, _, _, _) ->
    {string, lists:append([string(V) || V <- Values])};
function('starts-with', [A, B], _, _, _) ->
    {boolean, lists:prefix(string(B), string(A))};
function(contains, [A, B], _, _, _) ->
    {boolean, string:find(string(A), string(B)) =/= nomatch};
function('substring-before', [A, B], _, _, _) ->
    {string, before(string(A), string(B))};
function('substring-after', [A, B], _, _, _) ->
    {string, after_(string(A), string(B))};
function(translate, [A, B, C], _, _, _) ->
    {string, translate(string(A), string(B), string(C))};
function(boolean, [Value], _, _, _) ->
    {boolean, boolean(Value)};
function('not', [Value], _, _, _) ->
    {boolean, not boolean(Value)};
function(true, [], _, _, _) ->
    {boolean, true};
function(false, [], _, _, _) ->
    {boolean, false};
function(lang, [Lang], Context, _, _) ->
    {boolean, lang(string(Lang), Context)};
function(number, [], Context, _, _) ->
    {number, number({string, string_value(Context)})};
function(number, [Value], _, _, _) ->
    {number, number(Value)};
function(floor, [Value], _, _, _) ->
    {number, xfloor(number(Value))};
function(ceiling, [Value], _, _, _) ->
    {number, xceil(number(Value))};
function(Name, _, _, _, _) when
    Name =:= id; Name =:= 'string-length'; Name =:= 'normalize-space'; Name =:= sum; Name =:= round
->
    % These fail in OTP's xmerl.
    erlang:error({not_implemented, {function, Name}});
function(Name, _, _, _, _) ->
    exit({not_a_core_function, Name}).

first(_, []) -> [];
first(Fun, [N | _]) -> Fun(N).

% The string value OTP gives: an element's own text children, an attribute's, text's, a comment's or PI's value.
string_value(#n{node = #xmlElement{content = Content}}) ->
    lists:append([V || #xmlText{value = V} <- Content]);
string_value(#n{node = #xmlText{value = V}}) ->
    V;
string_value(#n{node = #xmlAttribute{value = V}}) ->
    V;
string_value(#n{node = #xmlComment{value = V}}) ->
    V;
string_value(#n{node = #xmlPI{value = V}}) ->
    V;
string_value(#n{node = Other}) ->
    erlang:error(function_clause, [Other]).

string({nodeset, []}) -> [];
string({nodeset, [N | _]}) -> string_value(N);
string({string, S}) -> S;
string({number, N}) when is_integer(N) -> integer_to_list(N);
string({number, N}) when is_float(N) -> float_to_list(N, [short]);
string({number, N}) -> atom_to_list(N);
string({boolean, B}) -> atom_to_list(B).

% A number: an integer or float from its text, else 'NaN'.
number({number, N}) ->
    N;
number({boolean, B}) ->
    case B of
        true -> 1;
        false -> 0
    end;
number(Value) ->
    Text = string:trim(string(Value)),
    case string:to_integer(Text) of
        {I, []} when is_integer(I) ->
            I;
        _ ->
            case string:to_float(Text) of
                {F, []} when is_float(F) -> F;
                _ -> 'NaN'
            end
    end.

xfloor(N) when is_number(N) -> erlang:floor(N);
xfloor(N) -> N.

xceil(N) when is_number(N) -> erlang:ceil(N);
xceil(N) -> N.

name(#n{node = #xmlElement{name = Name, nsinfo = {Prefix, Local}, namespace = NS}}) ->
    case lists:keyfind(Prefix, 1, NS#xmlNamespace.nodes) of
        {_, URI} -> "{" ++ atom_to_list(URI) ++ "}" ++ Local;
        false -> atom_to_list(Name)
    end;
name(#n{node = #xmlElement{name = Name}}) ->
    atom_to_list(Name);
name(#n{node = #xmlAttribute{name = Name}}) ->
    atom_to_list(Name);
name(#n{node = #xmlPI{name = Name}}) ->
    atom_to_list(Name);
name(#n{node = Other}) ->
    erlang:error(function_clause, [Other]).

local_name(#n{node = #xmlElement{nsinfo = {_, Local}}}) -> Local;
local_name(#n{node = #xmlAttribute{nsinfo = {_, Local}}}) -> Local;
local_name(N) -> name(N).

namespace_uri(#n{node = #xmlElement{nsinfo = {Prefix, _}, namespace = NS}}) ->
    case lists:keyfind(Prefix, 1, NS#xmlNamespace.nodes) of
        {_, URI} -> atom_to_list(URI);
        false -> []
    end;
namespace_uri(_) ->
    [].

% OTP's substring-before and substring-after keep the separator.
before(S, Sep) ->
    case string:find(S, Sep) of
        nomatch -> [];
        Rest -> lists:sublist(S, length(S) - length(Rest)) ++ Sep
    end.

after_(S, Sep) ->
    case string:find(S, Sep) of
        nomatch -> [];
        Rest -> Rest
    end.

translate(S, From, To) ->
    lists:append([translated(C, From, To) || C <- S]).

translated(C, From, To) ->
    case string:chr(From, C) of
        0 -> [C];
        I when I =< length(To) -> [lists:nth(I, To)];
        _ -> []
    end.

lang(Lang, #n{node = #xmlElement{language = Language}}) ->
    string:lowercase(Language) =:= string:lowercase(Lang);
lang(_, _) ->
    false.

axis(_, _, _) -> erlang:error({not_implemented, axis, 3}).
axis(_, _, _, _) -> erlang:error({not_implemented, axis, 4}).
eval_path(_, _, _) -> erlang:error({not_implemented, eval_path, 3}).
string(_, _, _, _, _) -> erlang:error({not_implemented, string, 5}).
write_node(_) -> erlang:error({not_implemented, write_node, 1}).
