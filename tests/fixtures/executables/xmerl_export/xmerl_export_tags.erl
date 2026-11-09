%% A callback module of xmerl's export inheriting xmerl_xml (plan 21 step MIM3c): it tells each call it gets and
%% writes the b element itself.
-module(xmerl_export_tags).
-export(['#xml-inheritance#'/0, '#root#'/4, '#element#'/5, '#text#'/1, '#cdata#'/1, '#comment#'/1, b/4]).

'#xml-inheritance#'() -> [xmerl_xml].

'#root#'(Data, Attrs, Parents, E) ->
    told('#root#', [Data, Attrs, Parents, E]),
    xmerl_xml:'#root#'(Data, Attrs, Parents, E).

'#element#'(Tag, Data, Attrs, Parents, E) ->
    told('#element#', [Tag, Data, Attrs, Parents, E]),
    xmerl_xml:'#element#'(Tag, Data, Attrs, Parents, E).

'#text#'(Text) ->
    told('#text#', [Text]),
    xmerl_xml:'#text#'(Text).

'#cdata#'(Text) ->
    told('#cdata#', [Text]),
    xmerl_xml:'#cdata#'(Text).

'#comment#'(Text) ->
    told('#comment#', [Text]),
    xmerl_xml:'#comment#'(Text).

b(Data, Attrs, Parents, E) ->
    told(b, [Data, Attrs, Parents, E]),
    ["<B>", Data, "</B>"].

% The scanned records' xmlbase is the current directory, which prints as "$CWD".
told(Function, Args) ->
    {ok, Cwd} = file:get_cwd(),
    io:format("  call ~w ~w~n", [Function, cwd(Cwd, Args)]).

cwd(Cwd, Value) when is_list(Value) ->
    case lists:prefix(Cwd, Value) andalso Value =/= [] of
        true -> "$CWD" ++ lists:nthtail(length(Cwd), Value);
        false -> [cwd(Cwd, V) || V <- Value]
    end;
cwd(Cwd, Value) when is_tuple(Value) ->
    list_to_tuple(cwd(Cwd, tuple_to_list(Value)));
cwd(_, Value) ->
    Value.
