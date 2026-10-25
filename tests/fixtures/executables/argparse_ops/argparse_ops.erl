-module(argparse_ops).
-export([main/1, handle_map/1, handle_pos/3]).

%% argparse (plan 21 step LIB70): parsing options, flags, positional
%% arguments, nargs, actions, types and sub-commands; parser errors and
%% format_error/1; command validation errors; help texts (default layout,
%% templates, wrapping, argument help forms and types); run/3 with every
%% handler form, and run/3 failing with an error and the help text.
main([]) ->
    basics(),
    kinds(),
    commands(),
    prefixes(),
    validation(),
    helps(),
    defaults(),
    io:format("argparse_ops: ok~n");
main(["run" | Args]) ->
    show(run, argparse:run(Args, runner(), #{progname => "runner"})),
    io:format("argparse_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, canon(Value)]).

id(X) -> X.

%% Maps as sorted lists and funs as an atom, so the output does not depend
%% on the map order or fun printing.
canon(Map) when is_map(Map) -> {map, lists:sort([{canon(K), canon(V)} || K := V <- Map])};
canon(Fun) when is_function(Fun) -> 'fun';
canon([H | T]) -> [canon(H) | canon(T)];
canon(Tuple) when is_tuple(Tuple) -> list_to_tuple(canon(tuple_to_list(Tuple)));
canon(Other) -> Other.

opts() -> #{progname => "tool"}.

parse(Label, Args, Command) -> parse(Label, Args, Command, opts()).

parse(Label, Args, Command, Options) ->
    Result = argparse:parse(Args, Command, Options),
    case Result of
        {ok, Map, Path, Sub} -> show(Label, {ok, Map, Path, lists:sort(maps:keys(Sub))});
        _ -> show(Label, Result)
    end,
    case Result of
        {error, {_, _, _, _} = Reason} ->
            Text = argparse:format_error(Reason),
            io:format("~s text ~ts~n", [Label, Text]),
            io:format("~s shape ~w~n", [Label, Text]);
        _ ->
            ok
    end.

basic() ->
    #{
        arguments => [
            #{name => verbose, short => $v, action => count},
            #{name => force, short => $f, long => "-force", type => boolean},
            #{name => out, short => $o, long => "out", help => "output file"},
            #{
                name => level,
                long => "level",
                type => {integer, [{min, 1}, {max, 5}]},
                default => 3
            },
            #{name => src, help => "source"},
            #{name => rest, nargs => list, required => false}
        ]
    }.

basics() ->
    parse(positional, ["file.txt"], basic()),
    parse(count, ["-v", "-v", "x"], basic()),
    parse(combined, ["-vvf", "x"], basic()),
    parse(short_value, ["-ofile", "x"], basic()),
    parse(long, ["-out", "a.txt", "x"], basic()),
    parse(long_equals, ["-out=b.txt", "x"], basic()),
    parse(boolean, ["--force", "x"], basic()),
    parse(boolean_false, ["--force", "false", "x"], basic()),
    parse(boolean_other, ["--force", "other"], basic()),
    parse(level, ["-level", "4", "x"], basic()),
    parse(level_max, ["-level", "9", "x"], basic()),
    parse(level_min, ["-level", "0", "x"], basic()),
    parse(level_text, ["-level", "abc", "x"], basic()),
    parse(missing, [], basic()),
    parse(rest, ["x", "y", "z"], basic()),
    parse(unknown, ["x", "-unknown"], basic()),
    parse(negative, ["-5"], basic()),
    parse(no_value, ["x", "-out"], basic()),
    parse(option_value, ["x", "-out", "-v"], basic()),
    parse(negative_value, ["x", "-out", "-3"], basic()),
    parse(binaries, [<<"x">>, <<"-v">>, [<<"-o">>, "ut"], "y"], basic()),
    parse(global_default, ["x"], basic(), #{progname => tool, default => none}),
    parse(dash, ["-"], basic()),
    parse(digit_flag, ["-1", "-2.5"], #{
        arguments => [#{name => one, short => $1, type => boolean}, #{name => n}]
    }),
    parse(digit_long, ["-2.5"], #{arguments => [#{name => two, long => "2.5", type => boolean}]}),
    parse(
        positional_kinds,
        ["a", "b", "c", "d", "e"],
        #{
            arguments => [
                #{name => two, nargs => 2},
                #{name => opt, nargs => 'maybe'},
                #{name => tail, nargs => all}
            ]
        }
    ),
    parse(
        positional_defaults,
        ["-q"],
        #{
            arguments => [
                #{name => first, required => false, default => def},
                #{name => second, required => false},
                #{name => q, short => $q, type => boolean},
                #{name => third, default => 3}
            ]
        }
    ),
    parse(
        catch_all,
        ["-x", "y"],
        #{
            arguments => [
                #{name => skip, required => false, default => s}, #{name => all, nargs => all}
            ]
        }
    ),
    parse(required_option, [], #{arguments => [#{name => r, long => "r", required => true}]}),
    parse(optional_positional, [], #{arguments => [#{name => p, required => false}]}).

custom("bad") -> error(badarg);
custom("throw") -> throw(custom_thrown);
custom("crash") -> error(crashed);
custom(Other) -> {custom, Other}.

kind() ->
    #{
        arguments => [
            #{name => pair, long => "pair", nargs => 2, type => integer},
            #{name => many, long => "many", nargs => nonempty_list},
            #{name => opt, long => "opt", nargs => 'maybe', type => integer},
            #{name => const, long => "const", nargs => {'maybe', fixed}},
            #{name => tag, short => $t, action => append},
            #{name => mark, short => $m, action => {append, seen}},
            #{name => set, short => $s, action => {store, on}},
            #{name => ext, long => "ext", nargs => list, action => extend},
            #{name => fl, long => "fl", type => {float, [{min, 0.5}]}},
            #{name => ch, long => "ch", type => {string, ["red", "green"]}},
            #{name => re, long => "re", type => {string, "^[a-z]+$"}},
            #{name => rex, long => "rex", type => {string, "^A", [caseless]}},
            #{name => bin, long => "bin", type => binary},
            #{name => binch, long => "binch", type => {binary, [<<"a">>, <<"b">>]}},
            #{name => binre, long => "binre", type => {binary, <<"^[0-9]+$">>}},
            #{name => binrex, long => "binrex", type => {binary, <<"^z">>, [caseless]}},
            #{name => at, long => "at", type => atom},
            #{name => atu, long => "atu", type => {atom, unsafe}},
            #{name => atc, long => "atc", type => {atom, [red, green]}},
            #{name => cu, long => "cu", type => {custom, fun custom/1}},
            #{name => cus, long => "cus", nargs => 2, type => {custom, fun custom/1}},
            #{name => ich, long => "ich", type => {integer, [1, 2, 3]}},
            #{name => fch, long => "fch", type => {float, [1.5, 2.5]}},
            #{name => maybool, long => "maybool", nargs => 'maybe', type => boolean},
            #{name => all, long => "all", nargs => all}
        ]
    }.

kinds() ->
    parse(pair, ["-pair", "1", "2"], kind()),
    parse(pair_short, ["-pair", "1"], kind()),
    parse(pair_text, ["-pair", "1", "x"], kind()),
    parse(many_none, ["-many"], kind()),
    parse(many, ["-many", "a", "b", "-t", "c"], kind()),
    parse(opt_none, ["-opt"], kind()),
    parse(opt, ["-opt", "7"], kind()),
    parse(opt_option, ["-opt", "-t", "z"], kind()),
    parse(opt_negative, ["-opt", "-7"], kind()),
    parse(const_none, ["-const"], kind()),
    parse(const, ["-const", "v"], kind()),
    parse(append, ["-t", "a", "-t", "b"], kind()),
    parse(append_const, ["-m", "-m", "-s"], kind()),
    parse(combined_consts, ["-mms"], kind()),
    parse(extend, ["-ext", "a", "b", "-ext", "c"], kind()),
    parse(float_min, ["-fl", "0.25"], kind()),
    parse(float_integer, ["-fl", "2"], kind()),
    parse(float, ["-fl", "1.5"], kind()),
    parse(float_text, ["-fl", "x"], kind()),
    parse(choice, ["-ch", "red"], kind()),
    parse(choice_bad, ["-ch", "blue"], kind()),
    parse(re, ["-re", "abc"], kind()),
    parse(re_bad, ["-re", "ab1"], kind()),
    parse(re_options, ["-rex", "abc"], kind()),
    parse(re_options_bad, ["-rex", "xyz"], kind()),
    parse(binary, ["-bin", "abc"], kind()),
    parse(binary_choice, ["-binch", "a"], kind()),
    parse(binary_choice_bad, ["-binch", "c"], kind()),
    parse(binary_re, ["-binre", "123"], kind()),
    parse(binary_re_bad, ["-binre", "12a"], kind()),
    parse(binary_re_options, ["-binrex", "Zed"], kind()),
    parse(binary_re_options_bad, ["-binrex", "yes"], kind()),
    parse(atom, ["-at", "ok"], kind()),
    parse(atom_unknown, ["-at", "not_an_existing_atom_qq"], kind()),
    parse(atom_unsafe, ["-atu", "fresh_atom_qq"], kind()),
    parse(atom_choice, ["-atc", "green"], kind()),
    parse(atom_choice_bad, ["-atc", "ok"], kind()),
    parse(atom_choice_unknown, ["-atc", "not_an_existing_atom_zz"], kind()),
    parse(custom, ["-cu", "x"], kind()),
    parse(custom_bad, ["-cu", "bad"], kind()),
    parse(custom_throw, ["-cu", "throw"], kind()),
    parse(custom_pair, ["-cus", "a", "b", "-cu", "c"], kind()),
    parse(custom_pair_bad, ["-cus", "a", "bad"], kind()),
    parse(integer_choice, ["-ich", "2"], kind()),
    parse(integer_choice_bad, ["-ich", "4"], kind()),
    parse(float_choice, ["-fch", "2.5"], kind()),
    parse(float_choice_bad, ["-fch", "2.0"], kind()),
    parse(maybe_boolean, ["-maybool", "false"], kind()),
    parse(maybe_boolean_none, ["-maybool"], kind()),
    parse(all, ["-all", "a", "-t", "c"], kind()),
    parse(
        custom_bif,
        ["-cu", "x"],
        #{arguments => [#{name => cu, long => "cu", type => {custom, fun erlang:binary_to_list/1}}]}
    ),
    try argparse:parse(["-cu", "crash"], kind(), opts()) of
        Result -> show(custom_crash, {returned, Result})
    catch
        error:Reason -> show(custom_crash, {error, Reason})
    end.

tree() ->
    #{
        arguments => [#{name => debug, short => $d, type => boolean}],
        commands => #{
            "start" => #{
                arguments => [#{name => name}],
                help => "Start a service",
                handler => fun handle_map/1
            },
            "stop" => #{help => hidden},
            "db" => #{
                commands => #{
                    "migrate" => #{arguments => [#{name => steps, type => integer, default => 1}]},
                    "drop" => #{help => <<"Drop it">>}
                },
                help => "Database"
            }
        }
    }.

commands() ->
    parse(no_subcommand, [], tree()),
    parse(start, ["-d", "start", "svc"], tree()),
    parse(start_missing, ["start"], tree()),
    parse(db, ["db"], tree()),
    parse(migrate, ["db", "migrate", "3"], tree()),
    parse(migrate_default, ["db", "-d", "migrate"], tree()),
    parse(migrate_bad, ["db", "migrate", "x"], tree()),
    parse(bogus, ["bogus"], tree()),
    parse(stop, ["stop"], tree()),
    parse(optional_handler, [], #{commands => #{"a" => #{}}, handler => optional}).

prefixes() ->
    parse(plus, ["+v", "x"], basic(), #{progname => "t", prefixes => "+"}),
    parse(plus_minus, ["-5", "+v"], basic(), #{progname => "t", prefixes => "+"}),
    parse(two_prefixes, ["+v", "-v", "x"], basic(), #{progname => "t", prefixes => "+-"}).

invalid(Label, Command) -> invalid(Label, Command, opts()).

invalid(Label, Command, Options) ->
    try argparse:parse([], Command, Options) of
        Result -> show(Label, {returned, Result})
    catch
        error:Reason -> show(Label, {error, Reason})
    end.

validation() ->
    invalid(arguments_not_list, #{arguments => id(foo)}),
    invalid(no_name, #{arguments => [#{short => $a}]}),
    invalid(argument_not_map, #{arguments => [id(1)]}),
    invalid(bad_help, #{arguments => [#{name => a, help => 1}]}),
    invalid(bad_help_template, #{arguments => [#{name => a, help => {"a", [type, 1]}}]}),
    invalid(bad_long, #{arguments => [#{name => a, long => 5}]}),
    invalid(bad_required, #{arguments => [#{name => a, required => yes}]}),
    invalid(bad_short, #{arguments => [#{name => a, short => -1}]}),
    invalid(bad_action, #{arguments => [#{name => a, action => bogus}]}),
    invalid(bad_extend, #{arguments => [#{name => a, action => extend}]}),
    invalid(custom_extend, #{
        arguments => [#{name => a, action => extend, type => {custom, fun custom/1}}]
    }),
    invalid(bad_type, #{arguments => [#{name => a, type => foo}]}),
    invalid(bad_validator, #{arguments => [#{name => a, type => {float, [{min, 1}]}}]}),
    invalid(bad_int_validator, #{arguments => [#{name => a, type => {integer, [{low, 1}]}}]}),
    invalid(bad_atom_choice, #{arguments => [#{name => a, type => {atom, [1]}}]}),
    invalid(bad_nargs, #{arguments => [#{name => a, nargs => 0}]}),
    invalid(short_conflict, #{arguments => [#{name => a, short => $a}, #{name => b, short => $a}]}),
    invalid(long_conflict, #{arguments => [#{name => a, long => "x"}, #{name => b, long => "x"}]}),
    invalid(
        nested_conflict,
        #{
            arguments => [#{name => a, short => $a}],
            commands => #{"sub" => #{arguments => [#{name => b, short => $a}]}}
        }
    ),
    invalid(prefix_name, #{commands => #{"-x" => #{}}}),
    invalid(command_not_map, #{commands => #{"x" => id(1)}}),
    invalid(commands_not_map, #{commands => id([1])}),
    invalid(bad_handler, #{handler => id(1)}),
    invalid(bad_command_help, #{help => id(1)}),
    invalid(bad_command_template, #{help => [usage, id(1)]}),
    invalid(good_template, #{help => ["text", usage, <<"bin">>, options]}),
    invalid(bad_progname, #{}, #{progname => id(1)}),
    invalid(binary_progname, #{}, #{progname => <<"bin">>}),
    show(default_progname, is_list(argparse:validate(#{}))),
    show(validate, argparse:validate(#{}, #{progname => "v"})).

help(Label, Command, Options) ->
    Text = argparse:help(Command, Options),
    io:format("~s~n~ts~s end~n", [Label, Text, Label]).

options() ->
    #{
        arguments => [
            #{
                name => req,
                short => $r,
                long => "-req",
                required => true,
                help => "required option"
            },
            #{name => flag, short => $f, type => boolean, help => "a flag"},
            #{name => flag2, short => $g, long => "-gee", type => boolean},
            #{name => longflag, long => "-long-flag", type => boolean, required => true},
            #{name => count, short => $c, action => count, help => ""},
            #{
                name => ints,
                long => "-ints",
                type => {integer, [1, 2, 3]},
                help => {"[--ints I]", [type, " choice"]}
            },
            #{name => floats, long => "-floats", type => {float, [1.5, 2.0]}},
            #{
                name => range,
                long => "-range",
                type => {integer, [{min, 1}, {max, 9}]},
                default => 5
            },
            #{name => low, long => "-low", type => {float, [{min, 0.5}]}},
            #{name => high, long => "-high", type => {integer, [{max, 7}]}},
            #{name => plain, long => "-plain", type => {integer, []}},
            #{name => re, long => "-re", type => {string, "^a"}, help => <<"binary help">>},
            #{name => rex, long => "-rex", type => {string, "^a", [caseless]}},
            #{name => bre, long => "-bre", type => {binary, <<"^b">>}},
            #{name => brex, long => "-brex", type => {binary, <<"^b">>, []}},
            #{name => sch, long => "-sch", type => {string, ["x", "y"]}},
            #{name => bch, long => "-bch", type => {binary, [<<"x">>, <<"y">>]}},
            #{name => at, long => "-at", type => atom, default => none},
            #{name => atu, long => "-atu", type => {atom, unsafe}},
            #{name => atc, long => "-atc", type => {atom, [x, y]}},
            #{name => fl, long => "-fl", type => float, default => 1.5},
            #{name => bin, long => "-bin", type => binary, default => <<"b">>},
            #{
                name => cu,
                long => "-cu",
                type => {custom, fun custom/1},
                help => {"[--cu C]", fun() -> "from a fun" end}
            },
            #{name => hid, long => "-hidden", help => hidden},
            #{name => list, long => "-list", nargs => list, default => ["a", "b"]},
            #{name => tup, long => "-tup", default => {1, "two"}},
            #{name => app, short => $a, action => append, required => true},
            #{
                name => this_is_a_really_long_option_name,
                long => "-this-is-a-really-long-option-name"
            },
            #{name => pos, help => "positional\nsecond line"},
            #{name => "string_name", nargs => nonempty_list, required => false},
            #{
                name => <<"bin_name">>,
                nargs => 'maybe',
                help => {"<bin>", [default, "!"]},
                default => x
            }
        ]
    }.

helps() ->
    help(basic, basic(), opts()),
    help(tree, tree(), opts()),
    help(tree_db, tree(), #{progname => "tool", command => ["db"]}),
    help(tree_migrate, tree(), #{progname => "tool", command => ["tool", "db", "migrate"]}),
    help(tree_narrow, tree(), #{progname => "tool", columns => 30}),
    help(options, options(), opts()),
    help(options_narrow, options(), #{progname => "tool", columns => 40}),
    help(options_plus, options(), #{progname => "tool", prefixes => "+-"}),
    help(empty, #{}, opts()),
    help(text_help, #{help => "Line one\nLine two is a little longer than the rest"}, #{
        progname => "tool", columns => 20
    }),
    help(binary_help, #{help => <<"Binary help">>, arguments => [#{name => x}]}, opts()),
    help(hidden_help, #{help => hidden, arguments => [#{name => x}]}, opts()),
    help(
        template,
        #{
            help => ["Custom intro\n", usage, "\n", options, <<"bin tail\n">>, arguments, commands],
            arguments => [#{name => x, help => "the x"}, #{name => o, short => $o}],
            commands => #{"c" => #{help => "a command"}}
        },
        opts()
    ),
    Many = #{commands => maps:from_list([{[C], #{help => [C]}} || C <- "abcde"])},
    help(many_commands, Many, opts()),
    help(nested_many, #{commands => #{"top" => Many}}, #{progname => "tool", command => ["top"]}),
    show(
        shape,
        argparse:help(
            #{arguments => [#{name => x, short => $x}, #{name => y}], help => "h"}, opts()
        )
    ),
    show(help_default_progname, is_list(lists:flatten(io_lib:format("~ts", [argparse:help(#{})])))).

defaults() ->
    show(parse2, element(1, argparse:parse(["a"], #{arguments => [#{name => a}]}))),
    show(
        format_unknown,
        lists:flatten(argparse:format_error({["p"], undefined, undefined, <<"oops">>}))
    ),
    show(
        format_detail,
        lists:flatten(argparse:format_error({["p", "q"], #{name => n}, "v", "is bad"}))
    ).

handle_map(Map) ->
    show(handle_map, Map),
    map_done.

handle_pos(T, A, B) ->
    show(handle_pos, {T, A, B}),
    pos_done.

runner() ->
    #{
        arguments => [#{name => top, short => $t, type => boolean}],
        commands => #{
            "fun" => #{arguments => [#{name => x}], handler => fun(M) -> {fun_done, canon(M)} end},
            "map" => #{arguments => [#{name => x}], handler => {?MODULE, handle_map}},
            "pos" => #{
                arguments => [#{name => x}, #{name => y, required => false}],
                handler => {?MODULE, handle_pos, none}
            },
            "anon" => #{
                arguments => [#{name => x, required => false}, #{name => y, required => false}],
                handler => {fun(T, A, B) -> {anon_done, T, A, B} end, missing}
            }
        }
    }.
