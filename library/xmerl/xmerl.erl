%% OTP's xmerl export (plan 21 step MIM3c, docs/libraries/xmerl.md#xmerl-export): records, or simple forms expanded to
%% records, exported bottom-up through a callback module and those it inherits from: Tag/4 where one exports it,
%% else '#element#'/5, '#text#'/1, '#cdata#'/1 and '#comment#'/1 (processing instructions left out), then '#root#'/4
%% with the export's options as attributes.
-module(xmerl).
-include("xmerl.types.hrl").
-export([
    callbacks/1,
    export/2,
    export/3,
    export_content/2,
    export_element/2,
    export_simple/2,
    export_simple/3,
    export_simple_content/2,
    export_simple_element/2
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([export_element/3]).

export(Content, Callback) ->
    export(Content, Callback, []).

export(Content, Callback, RootAttributes) when is_list(Content) ->
    root(export_content(Content, Callback), RootAttributes, Callback).

export_simple(Content, Callback) ->
    export_simple(Content, Callback, []).

export_simple(Content, Callback, RootAttributes) when is_list(Content) ->
    root(export_simple_content(Content, Callback), RootAttributes, Callback).

export_content(Content, Callback) ->
    content(Content, callbacks(Callback)).

export_element(Element, Callback) ->
    export_node(Element, callbacks(Callback)).

export_simple_content(Content, Callback) ->
    export_content(expand(Content, []), Callback).

export_simple_element(Element, Callback) ->
    [Expanded] = expand([Element], []),
    export_element(Expanded, Callback).

% The callback module and those it inherits from, nearest first.
callbacks(Module) ->
    % The literal fun links into programs the callback module export_simple's callers name only as data.
    _ = fun xmerl_xml:'#root#'/4,
    [Module | lists:append([callbacks(M) || M <- Module:'#xml-inheritance#'()])].

% The document: '#root#' with the options as the attributes of a '#root#' element.
root(Data, RootAttributes, Callback) ->
    Parents = [{'#root#', 1}],
    Attributes = [
        #xmlAttribute{name = Name, parents = Parents, pos = Pos, value = attribute_value(Value)}
     || {Pos, {Name, Value}} <- lists:enumerate(RootAttributes)
    ],
    Root = #xmlElement{name = '#root#', pos = 1, attributes = Attributes, language = [], xmlbase = []},
    call(callbacks(Callback), '#root#', [Data, Attributes, [], Root]).

content(Content, Callbacks) ->
    [Out || Node <- Content, Out <- node_out(Node, Callbacks)].

node_out(#xmlPI{}, _) -> [];
node_out(Node, Callbacks) -> [export_node(Node, Callbacks)].

export_node(#xmlText{value = Value, type = cdata}, Callbacks) ->
    call(Callbacks, '#cdata#', [Value]);
export_node(#xmlText{value = Value}, Callbacks) ->
    call(Callbacks, '#text#', [lists:flatten(Value)]);
export_node(#xmlComment{value = Value}, Callbacks) ->
    call(Callbacks, '#comment#', [Value]);
export_node(#xmlElement{name = Tag, attributes = Attributes, content = Content, parents = Parents} = E, Callbacks) ->
    Data = content(Content, Callbacks),
    case [M || M <- Callbacks, erlang:function_exported(M, Tag, 4)] of
        [M | _] -> M:Tag(Data, Attributes, Parents, E);
        [] -> call(Callbacks, '#element#', [Tag, Data, Attributes, Parents, E])
    end.

% A callback function from the first module of the chain that exports it.
call(Callbacks, Function, Args) ->
    Arity = length(Args),
    case [M || M <- Callbacks, erlang:function_exported(M, Function, Arity)] of
        [M | _] -> apply(M, Function, Args);
        [] -> apply(hd(Callbacks), Function, Args)
    end.

%% --- Simple forms as records ---

% Simple content as records under Parents: {Tag, Attributes, Content}, {Tag, Content}, Tag, text, or records.
expand(Content, Parents) ->
    [simple_node(Item, Pos, Parents) || {Pos, Item} <- lists:enumerate(Content)].

simple_node({Tag, Attributes, Content}, Pos, Parents) when is_atom(Tag) ->
    element(Tag, Attributes, Content, Pos, Parents);
simple_node({Tag, Content}, Pos, Parents) when is_atom(Tag) ->
    element(Tag, [], Content, Pos, Parents);
simple_node(Tag, Pos, Parents) when is_atom(Tag) ->
    element(Tag, [], [], Pos, Parents);
simple_node(Text, Pos, Parents) when is_list(Text) ->
    #xmlText{parents = Parents, pos = Pos, value = Text};
% Records take their place's parents and position.
simple_node(#xmlElement{} = E, Pos, Parents) ->
    E#xmlElement{parents = Parents, pos = Pos};
simple_node(#xmlText{} = T, Pos, Parents) ->
    T#xmlText{parents = Parents, pos = Pos};
simple_node(#xmlComment{} = C, Pos, Parents) ->
    C#xmlComment{parents = Parents, pos = Pos};
simple_node(#xmlPI{} = P, Pos, Parents) ->
    P#xmlPI{parents = Parents, pos = Pos};
simple_node(Other, Pos, Parents) ->
    erlang:error(function_clause, [Other, Pos, Parents]).

element(Tag, Attributes, Content, Pos, Parents) ->
    Own = [{Tag, Pos} | Parents],
    #xmlElement{
        name = Tag,
        parents = Parents,
        pos = Pos,
        attributes = [
            #xmlAttribute{name = attribute_name(Name), parents = Own, pos = I, value = attribute_value(Value)}
         || {I, {Name, Value}} <- lists:enumerate(Attributes)
        ],
        content = expand(Content, Own),
        language = [],
        xmlbase = []
    }.

attribute_name(Name) when is_atom(Name) -> Name;
attribute_name(_) -> erlang:error(badarg).

attribute_value(Value) when is_list(Value) -> lists:flatten(Value);
attribute_value(Value) when is_binary(Value) -> binary_to_list(Value);
attribute_value(Value) when is_atom(Value) -> atom_to_list(Value);
attribute_value(Value) when is_integer(Value) -> integer_to_list(Value);
attribute_value(_) -> erlang:error(badarg).

export_element(_, _, _) -> erlang:error({not_implemented, export_element, 3}).
