-module(node_code).
-export([main/1]).

%% The answers of a non-distributed node and of loaded code (plan step 41H), against an OTP node started without
%% distribution.
main(_) ->
    node_answers(),
    code_answers(),
    io:format("node_code: ok~n").

node_answers() ->
    show({node(), nodes(), is_alive(), node(self())}),
    show([nodes(T) || T <- [visible, hidden, connected, this, known]]),
    show(nodes([this, visible])),
    show(nodes(this, #{connection_id => true})),
    show(nodes(visible, #{node_type => true})),
    show(run(fun() -> nodes(bogus) end)),
    show(run(fun() -> monitor_node(other@host, true) end)),
    show(run(fun() -> erlang:monitor_node(other@host, true, [allow_passive_connect]) end)),
    show(run(fun() -> monitor_node(1, true) end)),
    show(disconnect_node(other@host)).

code_answers() ->
    show([erlang:module_loaded(M) || M <- [?MODULE, lists, no_such_mod]]),
    show(run(fun() -> erlang:module_loaded(1) end)),
    show(lists:member(?MODULE, erlang:loaded()) andalso is_list(erlang:pre_loaded())),
    show(erlang:delete_module(no_such_mod)),
    show(erlang:get_module_info(?MODULE, module)),
    show(lists:member({main, 1}, erlang:get_module_info(?MODULE, exports))),
    show(is_list(erlang:get_module_info(?MODULE))),
    show(erlang:prepare_loading(foo, <<"bad">>)),
    show(erlang:finish_loading([])).

show(Value) -> io:format("~p~n", [Value]).

% The value of Fun, or {error, Reason}.
run(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
