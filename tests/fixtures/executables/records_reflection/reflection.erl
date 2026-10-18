-module(reflection).
-export([main/1]).
-export_record([point]).
-compile(nowarn_unused_record).

%% records and erl_features (plan 21 step LIB8B): native record reflection over compiled and created records, the
%% definitions with folded defaults, every error shape, and the features each module was compiled with.
-record(#point{x = 0, y = {a, "s"}}).
-record(#bare{a, b = 1}).
-record #empty{}.
-record(#folded{
    a = 1 + 2,
    b = -3,
    c = 2.0 * 3,
    d = 1 bsl 70,
    e = not true,
    f = 1 < 2,
    g = 10 / 4,
    h = <<"bin">>,
    i = $a,
    j = (1 + 1) * 3,
    k = #{k => {1, <<"z">>}},
    l = [x | y],
    m = <<"ab"/utf16>>,
    n = true xor false,
    o = bnot 0,
    p = 7 rem 3
}).

%% With "used": the features of the program's own modules, which OTP answers from their .beam files (the oracle
%% loads none, so this run is authored from a node that has them).
main(["used"]) ->
    show(used, [erl_features:used(reflection), erl_features:used(featured)]),
    io:format("reflection: ok~n");
main(_) ->
    reflect(),
    create(),
    update(),
    definitions(),
    features(),
    io:format("reflection: ok~n").

reflect() ->
    P = #point{x = 5},
    B = #bare{a = 1},
    show(reflect, [
        records:get(x, P),
        records:get(y, P),
        records:get_module(P),
        records:get_name(P),
        records:get_field_names(P),
        records:is_exported(P),
        records:get_field_names(B),
        records:is_exported(B),
        records:get_field_names(#empty{}),
        catching(fun() -> records:get(zz, P) end),
        catching(fun() -> records:get(1, P) end),
        catching(fun() -> records:get(x, id({x})) end),
        catching(fun() -> records:get_module(id(#{})) end),
        catching(fun() -> records:get_name(id(x)) end),
        catching(fun() -> records:get_field_names(id({x})) end),
        catching(fun() -> records:is_exported(id([])) end)
    ]).

create() ->
    R = records:create(test, a, [{z, 3}, {x, 1}, {y, 2}], #{is_exported => true}),
    show(create, [
        R,
        records:get_field_names(R),
        records:is_exported(R),
        records:get_module(R),
        records:get_name(R),
        records:get(x, R),
        R =:= records:create(test, a, [{z, 3}, {x, 1}, {y, 2}], #{is_exported => true}),
        R =:= records:create(test, a, [{x, 1}, {y, 2}, {z, 3}], #{is_exported => true}),
        R =:= records:create(test, a, [{z, 3}, {x, 1}, {y, 2}], #{is_exported => false}),
        R == records:create(test, a, [{z, 3.0}, {x, 1}, {y, 2}], #{is_exported => true}),
        records:create(reflection, point, [{x, 1}, {y, 2}], #{is_exported => true}) =:= #point{x = 1, y = 2},
        records:create(reflection, point, [{y, 2}, {x, 1}], #{is_exported => true}) =:= #point{x = 1, y = 2},
        records:create(reflection, point, [{x, 1}, {y, 2}], #{is_exported => false}) =:= #point{x = 1, y = 2},
        records:create(m, a, [], #{is_exported => false}),
        lists:sort([
            records:create(m, b, [], #{is_exported => false}),
            records:create(m, a, [{x, 1}], #{is_exported => true}),
            records:create(a, z, [], #{is_exported => false}),
            records:create(m, a, [{x, 0}], #{is_exported => true})
        ]),
        catching(fun() -> records:create(id(1), a, [], #{is_exported => true}) end),
        catching(fun() -> records:create(m, id(1), [], #{is_exported => true}) end),
        catching(fun() -> records:create(id(1), id(2), [], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, x, id(y)) end),
        catching(fun() -> records:create(m, a, [], #{}) end),
        catching(fun() -> records:create(m, a, [], #{is_exported => perhaps}) end),
        catching(fun() -> records:create(m, a, [], #{is_exported => true, other => 1}) end),
        catching(fun() -> records:create(m, a, [{1, 2}], #{}) end),
        catching(fun() -> records:create(m, a, [{x, 1}, {1, 2}], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{x, 1}, {x, 2}], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{x, 1}, {x, 2}, {1, 2}], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{x, 1, 2}], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [x], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, id(x), #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{x, 1} | y], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{1, 2}, x], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [x, {1, 2}], #{is_exported => true}) end),
        catching(fun() -> records:create(m, a, [{"x", 1}], #{is_exported => true}) end)
    ]).

update() ->
    P = #point{x = 5},
    B = #bare{a = 1},
    R = records:create(m, a, [{y, 2}, {x, 1}], #{is_exported => false}),
    show(update, [
        records:update(P, reflection, point, #{x => 10}),
        records:update(P, reflection, point, #{}),
        records:update(B, reflection, bare, #{a => 5, b => 6}),
        records:update(R, m, a, #{x => 10}),
        (records:update(P, reflection, point, #{y => 7}))#point.y,
        catching(fun() -> records:update(P, reflection, point, #{w => 1}) end),
        catching(fun() -> records:update(P, reflection, point, #{1 => 5, z => 1}) end),
        catching(fun() -> records:update(P, reflection, other, #{x => 1}) end),
        catching(fun() -> records:update(P, other, point, #{x => 1}) end),
        catching(fun() -> records:update(P, id(1), point, #{x => 1}) end),
        catching(fun() -> records:update(P, reflection, id(2), #{x => 1}) end),
        catching(fun() -> records:update(P, reflection, point, id(x)) end),
        catching(fun() -> records:update(P, reflection, other, id(x)) end),
        catching(fun() -> records:update(id({a}), reflection, point, #{}) end),
        catching(fun() -> records:update(id(x), reflection, other, id(x)) end)
    ]).

definitions() ->
    show(definitions, [
        records:get_definition(reflection, point),
        records:get_definition(reflection, bare),
        records:get_definition(reflection, empty),
        records:get_definition(reflection, folded),
        catching(fun() -> records:get_definition(reflection, nope) end),
        catching(fun() -> records:get_definition(nomodule, nope) end),
        catching(fun() -> records:get_definition(id(1), point) end),
        catching(fun() -> records:get_definition(reflection, id(1)) end),
        catching(fun() -> records:get_definition(lists, point) end)
    ]).

features() ->
    show(features, [
        erl_features:all(),
        erl_features:configurable(),
        erl_features:enabled(),
        lists:sort(maps:to_list(erl_features:info(maybe_expr))),
        lists:sort(maps:to_list(erl_features:info(compr_assign))),
        catching(fun() -> erl_features:info(nope) end),
        catching(fun() -> erl_features:info(id(1)) end),
        erl_features:keywords(),
        erl_features:keywords(maybe_expr),
        erl_features:keywords(compr_assign),
        catching(fun() -> erl_features:keywords(nope) end),
        erl_features:used(nomodule),
        erl_features:used("nofile.beam"),
        erl_features:used("nofile"),
        catching(fun() -> erl_features:used(id(<<"x">>)) end),
        catching(fun() -> erl_features:used(id(1)) end),
        lists:flatten(erl_features:short(maybe_expr)),
        featured:pairs()
    ]).

catching(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
