%% erl_parse (plan 21 step LIB31): forms, expressions, terms, types, the errors of the grammar and of its actions,
%% abstract/1,2, normalise/1, tokens/1,2 and the annotation functions, compared with OTP.
-module(erl_parse_forms).
-export([main/1, next_chunk/0]).

main(_) ->
    attributes(),
    records(),
    types(),
    specs(),
    docs(),
    functions(),
    patterns(),
    operators(),
    structures(),
    records_and_maps(),
    comprehensions(),
    control(),
    funs(),
    try_and_maybe(),
    binaries(),
    strings_and_sigils(),
    terms(),
    syntax_errors(),
    action_errors(),
    texts(),
    abstract(),
    normalise(),
    tokens(),
    precedences(),
    annotations(),
    yecc(),
    io:format("erl_parse_forms: ok~n").

show(Label, Term) ->
    io:format("~s: ~w~n", [Label, Term]).

%% Errors also show their message, flattened.
outcome({error, {Location, Module, Descriptor}} = Error) ->
    {Error, Location, lists:flatten(Module:format_error(Descriptor))};
outcome(Result) ->
    Result.

tokens_of(String) ->
    {ok, Tokens, _} = erl_scan:string(String, {1, 1}),
    Tokens.

%% Some inputs crash OTP's actions: the class and reason show instead.
guarded(Fun) ->
    try
        outcome(Fun())
    catch
        Class:Reason -> {Class, Reason}
    end.

form(Label, String) ->
    show(Label, guarded(fun() -> erl_parse:parse_form(tokens_of(String)) end)).

exprs(Label, String) ->
    show(Label, guarded(fun() -> erl_parse:parse_exprs(tokens_of(String)) end)).

term(Label, String) ->
    show(Label, guarded(fun() -> erl_parse:parse_term(tokens_of(String)) end)).

attributes() ->
    form("module", "-module(m)."),
    form("module with variables", "-module(m, [A, B])."),
    form("export", "-export([f/1, g/0])."),
    form("export none", "-export([])."),
    form("import", "-import(lists, [map/2, foldl/3])."),
    form("import_record", "-import_record(m, [a, b])."),
    form("file", "-file(\"x.erl\", 3)."),
    form("behaviour", "-behaviour(gen_server)."),
    form("compile", "-compile([export_all, {inline, [f/1]}])."),
    form("vsn", "-vsn(1)."),
    form("negative value", "-foo(-1)."),
    form("string value", "-foo(\"s\")."),
    form("list of farities", "-dialyzer({nowarn_function, [f/1, {g, 2}]})."),
    form("map value", "-foo(#{a => f/1})."),
    form("binary value", "-foo(<<1, 2>>)."),
    form("unparenthesized", "-foo bar."),
    form("unparenthesized expression", "-foo (a) + 1."),
    form("fun value", "-foo(fun lists:map/2)."),
    form("on_load", "-on_load(init/0)."),
    form("parenthesized value", "-foo((1))."),
    form("float value", "-foo(1.5).").

records() ->
    form("record", "-record(r, {a, b = 1, c :: integer(), d = 2 :: atom()})."),
    form("record unparenthesized", "-record r, {a}."),
    form("empty record", "-record(r, {})."),
    form("record typed unparenthesized", "-record r, {a :: x, b}."),
    form("record field expressions", "-record(r, {a = f(1) + 2, b = [x | y]})."),
    form("native record", "-record #r{a, b = 1}."),
    form("native record typed", "-record #r{a :: integer(), b = 1 :: atom()}."),
    form("native record parenthesized", "-record(#r{a})."),
    form("native record reserved name", "-record #case{a}."),
    form("native record var name", "-record #Rec{a}."),
    form("record attribute name", "-record(record, {a})."),
    form("record as spec", "-record r(A) -> A."),
    form("record as spec parenthesized", "-record(r(A) -> A).").

types() ->
    form("type", "-type t() :: integer()."),
    form("type with variable", "-type t(A) :: [A]."),
    form("opaque", "-opaque o() :: {a, b}."),
    form("nominal", "-nominal n() :: atom()."),
    form("type parenthesized", "-type(t() :: x)."),
    form("unions", "-type t() :: a | b | c."),
    form("annotated", "-type t() :: A :: integer() | B :: atom()."),
    form("ranges", "-type t() :: 1..10 | -1..1 | $a..$z | 1 + 2..3 * 4 | bnot 1 | 2 div 2."),
    form("atoms and lists", "-type t() :: 'x' | [] | [a] | [a, ...]."),
    form("maps", "-type t() :: #{} | #{a => b, c := d} | map()."),
    form("tuples", "-type t() :: {} | {a} | {a, b} | tuple()."),
    form("record types", "-type t() :: #r{} | #r{a :: x, b :: y}."),
    form("native record types", "-type t() :: #m:r{} | #m:r{a :: x} | #m:case{}."),
    form("binary types", "-type t() :: <<>> | <<_:8>> | <<_:_*8>> | <<_:8, _:_*4>>."),
    form("fun types", "-type t() :: fun() | fun((...) -> a) | fun(() -> a) | fun((a, b) -> c)."),
    form("remote types", "-type t() :: m:t() | m:t(a, b)."),
    form(
        "predefined and user", "-type t() :: integer(1) | list() | u() | u(a) | nonempty_list(a)."
    ),
    form("parenthesized type", "-type t() :: (a | b)."),
    form("variable union", "-type t(A) :: A | b."),
    form("integer singletons", "-type t() :: 1 | -1 | +2 | $c."),
    form("type multiplication", "-type t() :: 1 * 2 * 3 | 1 + 2 + 3 * 4 | 1 * 2 + 3."),
    form("unit type expression", "-type t() :: <<_:_*2*4>> | <<_:_*1+1>>."),
    form("base type expression", "-type t() :: <<_:8*2>> | <<_:1..2>>."),
    form("type prefix chain", "-type t() :: - - 1 | (1)..(2).").

specs() ->
    form("spec", "-spec f(integer()) -> atom()."),
    form("spec clauses", "-spec f(a) -> b; (c) -> d."),
    form("spec remote", "-spec m:f(A) -> A when A :: atom()."),
    form("spec constraints", "-spec f(X) -> Y when X :: a, Y :: b."),
    form("spec is_subtype", "-spec f(A) -> A when is_subtype(A, atom())."),
    form("spec parenthesized", "-spec(f() -> ok)."),
    form("callback", "-callback c(term()) -> ok."),
    form("spec any arity fun", "-spec f(fun((...) -> ok)) -> ok."),
    form("spec annotated arguments", "-spec f(Name :: atom(), Age :: integer()) -> ok.").

docs() ->
    form("doc string", "-doc \"text\"."),
    form("doc triple quoted", "-doc \"\"\"\n  text\n  \"\"\"."),
    form("doc false", "-doc false."),
    form("moduledoc hidden", "-moduledoc hidden."),
    form("doc map", "-doc #{since => <<\"1.0\">>, equiv => f(1)}."),
    form("moduledoc map equiv", "-moduledoc #{equiv => f(1)}."),
    form("doc file", "-doc {file, \"a.md\"}."),
    form("doc binary", "-doc <<\"bin\">>."),
    form("doc sigil", "-doc ~\"sigil\".").

functions() ->
    form("function", "f() -> ok."),
    form("function clauses", "f(X) when X > 0, is_integer(X); X < -1 -> X; f(_) -> 0."),
    form("function body", "f(A, B) -> C = A + B, D = C * 2, D."),
    form("quoted function name", "'my f'() -> ok."),
    form("guard sequences", "f(X) when is_atom(X); is_list(X), length(X) > 1 -> X."),
    form("guard calls", "f(X) when erlang:is_atom(X), element(1, X) =:= a -> X.").

patterns() ->
    form(
        "pattern forms",
        "f([H | T], {A, B}, <<X:8, Y/binary>>, #{k := V}, #r{a = 1}, \"s\" ++ R, -1, X = Y) -> ok."
    ),
    form("pattern operators", "f(1 + 2, -X, A = B, X * 2, [a] ++ L) -> ok."),
    form("record index pattern", "f(#r.a) -> ok."),
    form("native record patterns", "f(#_{a = X}, #m:r{b = Y}, #r{_ = '_'}) -> ok."),
    form("parenthesized pattern", "f((X)) -> X."),
    form("comparison pattern", "f(A == B) -> ok."),
    form("nested expressions in patterns", "f({g(1)}, [h()], <<(i()):8>>) -> ok."),
    form("map pattern", "f(#{a := 1, B := 2}) -> ok."),
    form("char and float patterns", "f($a, 1.5, 'atom', \"\") -> ok.").

operators() ->
    exprs("arithmetic", "1 + 2 * 3 - 4 / 5 div 6 rem 7."),
    exprs("bitwise", "a band b bor c bxor d bsl e bsr f."),
    exprs("logic", "a and b or c xor d."),
    exprs("short circuit", "a andalso b orelse c andalso d."),
    exprs("list operators", "[1] ++ [2] -- [3] ++ [4]."),
    exprs("comparisons", "a == b, c /= d, e =< f, g < h, i >= j, k > l, m =:= n, o =/= p."),
    exprs("prefix", "- - 1, + 2, not not a, bnot 3, -a * b, - a:b(c)."),
    exprs("match chain", "A = B = c."),
    exprs("send chain", "A ! B ! c."),
    exprs("match and send", "A = B ! c, D ! E = f."),
    exprs("catch", "catch a + b, a + catch b = c, catch 1."),
    exprs("remote calls", "m:f(1), M:F(), (m:f)(a), m:(f)(b), m:-f(c), a:b(c):d(e)."),
    exprs("calls", "f(), f(1)(2), (fun g/0)(), \"s\"(1), X(), (f())(1)."),
    exprs("remote record", "a:b#r.c."),
    exprs("comparison of sums", "a + b < c * d."),
    exprs("right associative", "a ++ b ++ c, a -- b -- c, a orelse b orelse c."),
    exprs("left associative", "a - b - c, a / b / c, a bsl b bsr c."),
    exprs("unary chain", "- + - bnot not 1."),
    exprs("negative literals", "-1, -1.5, -$a, +1."),
    exprs("chained multiplication", "a * b * c, a / b * c.").

structures() ->
    exprs("tuples", "{}, {a}, {a, b, c}."),
    exprs("lists", "[], [a], [a, b], [a | b], [a, b | c], [[a]]."),
    exprs("block", "begin a, b end."),
    exprs("variables", "X, _, _Y."),
    exprs("chars", "$a, $\\n, $\\x{1F600}."),
    exprs("numbers", "0, 1_000, 16#ff, 1.5e3, 123456789012345678901234567890."),
    exprs("atoms", "a, 'b c', 'end', '$end'."),
    exprs("parenthesized", "(a), ((b)), (a + b) * c.").

records_and_maps() ->
    exprs("record creation", "#r{}, #r{a = 1, b = 2}, #r{_ = x}, #case{a = 1}, #X{a = 1}."),
    exprs("record update", "X#r{a = 1}, X#r{a = 1}#r{b = 2}, #r{}#r{a = 1}."),
    exprs("record access", "X#r.a, X#r.a#r.b, #r.a, #r{}#r.a, (f())#r.a."),
    exprs("native records", "#_{a = 1}, #m:r{a = 1}, X#m:r.a, X#_.a, X#m:r{a = 1}, X#_{a = 1}."),
    exprs("record reserved names", "#if{}, X#end.a, X#when{a = 1}."),
    exprs("maps", "#{}, #{a => 1, b => 2}, #{a := 1}, #{{a, b} => [c]}."),
    exprs("map update", "X#{a => 1}, X#{a := 1}#{b => 2}, #{}#{a => 1}, (f())#{a => 1}."),
    exprs("map of records", "#{a => #r{}, b => X#r.a}."),
    exprs("map update of literals", "[]#{a => 1}, {a}#{b => 2}, <<>>#{c => 3}."),
    exprs("call of field", "X#r.a(1), X#{}(1), #r{}(1).").

comprehensions() ->
    exprs("list comprehension", "[X || X <- L, X > 0]."),
    exprs("multiple templates", "[X, Y || X <- L, Y <- M]."),
    exprs("strict generators", "[X || X <:- L], [X || <<X>> <:= B]."),
    exprs("binary generator", "[X || <<X:8>> <= B]."),
    exprs("map generator", "[{K, V} || K := V <- M], [K || K := _ <:- M]."),
    exprs("map comprehension", "#{K => V || K := V <- M}, #{K => 1, V => 2 || {K, V} <- L}."),
    exprs("binary comprehension", "<< <<X>> || X <- L >>, << X || <<X:8>> <= B >>."),
    exprs("zip", "[{X, Y} || X <- A && Y <- B], [X || X <- A && Y <- B && Z <- C, Y > Z]."),
    exprs("zip after filters", "[X || true, X <- A && Y <- B, true]."),
    exprs("generator patterns", "[X || {X, _} = T <- L, [Y | _] <- T]."),
    exprs("filters only", "[x || true, false]."),
    exprs("nested", "[[Y || Y <- X] || X <- L].").

control() ->
    exprs("if", "if X > 0 -> a; X < 0, Y -> b; true -> c end."),
    exprs("if guard sequence", "if a; b, c -> d end."),
    exprs("case", "case X of {a, B} when B > 0 -> B; _ -> 0 end."),
    exprs("case with expressions", "case f() of a + b -> c; X when X; Y -> d end."),
    exprs("receive", "receive X -> X; {Y} when Y > 1 -> Y end."),
    exprs("receive after", "receive X -> X after 10 -> ok end."),
    exprs("receive only after", "receive after 0 -> a, b end."),
    exprs("nested control", "case a of b -> if c -> d end end.").

funs() ->
    exprs("local fun", "fun f/1."),
    exprs("remote fun", "fun m:f/1, fun M:F/A, fun m:F/2, fun M:f/N."),
    exprs("anonymous fun", "fun (X) -> X end, fun () -> ok end."),
    exprs("fun clauses", "fun ({a}) when true -> b; (_) -> c end."),
    exprs("named fun", "fun F(0) -> 1; F(N) -> N * F(N - 1) end."),
    exprs("fun guards", "fun (X) when X > 0; X < -1 -> X end."),
    exprs("fun call", "(fun (X) -> X end)(1).").

try_and_maybe() ->
    exprs("try catch", "try f() catch throw:a -> 1; error:E:S -> S; E -> E; _:_ -> ok end."),
    exprs("try of", "try f() of X -> X; Y when Y -> Y catch C:R -> {C, R} end."),
    exprs("try after", "try a after b, c end."),
    exprs("try catch after", "try a catch _ -> b after c end."),
    exprs("try class variable", "try a catch C:R:S when is_atom(C) -> x end."),
    exprs("try catch patterns", "try a catch {x, Y} = Z -> Y; [_ | _] -> l end."),
    exprs("try body", "try a, b of c -> d catch e -> f end."),
    exprs("maybe", "maybe {ok, A} ?= f(), B = g(A), ok end."),
    exprs("maybe else", "maybe {ok, A} ?= f(), A else {error, E} -> E; _ -> x end."),
    exprs("maybe plain", "maybe a, b end."),
    exprs("maybe match chain", "maybe A = B ?= C = D end.").

binaries() ->
    exprs("binary", "<<>>, <<1>>, <<1, 2:4, X:N/integer-unit:8-little, \"s\"/utf8, (X + 1):8>>."),
    exprs("binary types", "<<X/binary, Y/bits, Z:16/signed-big-integer, F/float>>."),
    exprs("binary prefix", "<<-1, +X, bnot Y, not Z>>."),
    exprs("binary nested", "<<<<1>>/binary, (<<2>>)/binary>>."),
    exprs("binary size expressions", "<<X:(N * 8), Y:S/unit:4>>."),
    exprs("binary unit type", "<<X:4/unit:8>>, <<Y/integer-unit:1>>.").

strings_and_sigils() ->
    exprs("strings", "\"\", \"abc\", \"a\" \"b\" \"c\"."),
    exprs("sigils", "~\"x\", ~b\"y\", ~B\"z\", ~s\"w\", ~S\"v\", ~\"\"."),
    exprs("sigil delimiters", "~s[a], ~b(b), ~{c}, ~<d>, ~/e/, ~|f|, ~#g#, ~'h'."),
    exprs("triple quoted", "\"\"\"\n  a\n  \"\"\"."),
    exprs("sigil triple quoted", "~b\"\"\"\n  a\n  \"\"\".").

terms() ->
    term("atom", "a."),
    term("compound", "{a, [1, 2.5, \"s\", <<1, 2>>, #{a => 1}, -3, $c], {}}."),
    term("fun", "fun lists:map/2."),
    term("negatives", "[-1, -1.5, -$a, +1, +2.5, +$b]."),
    term("binaries", "[<<\"abc\">>, <<1:4>>, <<\"x\"/utf8>>, <<1.5/float>>, <<1:16/little>>]."),
    term("binary segments", "[<<-1:8/signed>>, <<1:2, 3:6>>, <<\"ab\"/utf16>>, <<255:8/unit:1>>]."),
    term("binary of binaries", "<<<<1, 2>>/binary, <<3:3>>/bits>>."),
    term("empty map", "#{}."),
    term("nested map", "#{a => #{b => [c]}}."),
    term("improper list", "[a | b]."),
    term("string", "\"abc\"."),
    term("variable", "X."),
    term("two expressions", "a, b."),
    term("call", "f()."),
    term("map exact", "#{a := 1}."),
    term("operator", "1 + 2."),
    term("double negative", "- -1."),
    term("syntax error", "1 + ."),
    term("float binary size", "<<1.5:32/float>>."),
    term("utf binary", "<<$a/utf32, 16#1F600/utf8>>.").

syntax_errors() ->
    form("missing body", "f() -> ."),
    form("missing paren", "f( -> ok."),
    form("missing value", "-module(."),
    form("two expressions", "f() -> a b."),
    form("missing case expression", "f() -> case of end."),
    form("no dot", "f() -> ok"),
    form("tokens after dot", "f() -> ok. g"),
    form("not a form", "1."),
    form("variable form", "X."),
    form("call in pattern", "f(a(b)) -> ok."),
    form("remote in pattern", "f(a:b) -> ok."),
    form("catch in pattern", "f(catch a) -> ok."),
    form("send in pattern", "f(a ! b) -> ok."),
    form("record update in pattern", "f(X#r{}) -> ok."),
    form("comprehension in pattern", "f([X || X <- Y]) -> ok."),
    form("map comprehension in pattern", "f(#{K => V || K := V <- M}) -> ok."),
    form("binary comprehension in pattern", "f(<<X || X <- Y>>) -> ok."),
    form("block in pattern", "f(begin a end) -> ok."),
    exprs("plus at end", "1 + ."),
    exprs("double remote", "a:b:c."),
    exprs("double comparison", "a < b < c."),
    exprs("double range comparison", "a =:= b == c."),
    exprs("record of call", "f()#r.a."),
    exprs("record of map", "X#{}#r.a."),
    exprs("map of record", "X#r.a#{}."),
    exprs("native record chain", "X#m:r.a#m:r.b."),
    exprs("anonymous record chain", "X#_.a#_.b."),
    exprs("record of remote call", "a:b()#r.c."),
    exprs("record index of variable", "#X.a."),
    exprs("record field not atom", "X#r.B."),
    exprs("binary expression element", "<<a + b>>."),
    exprs("binary record element", "<<X#r.a>>."),
    exprs("binary comprehension with size", "<<X:8 || X <- L>>."),
    exprs("binary comprehension prefix", "<<-X || X <- L>>."),
    exprs("empty qualifiers", "[x || ]."),
    exprs("unclosed tuple", "{a, b."),
    exprs("unclosed list", "[a, b."),
    exprs("tail then comprehension", "[a | b || c]."),
    exprs("binary generator of expression", "[x || <<a>> + b <= c]."),
    exprs("parenthesized binary generator", "[x || (<<a>>) <= c]."),
    exprs("map generator without arrow", "[x || a := b]."),
    exprs("question equals outside maybe", "a ?= b."),
    exprs("question equals nested", "maybe (a ?= b) end."),
    exprs("empty", ""),
    exprs("only dot", "."),
    exprs("open paren", "f("),
    exprs("missing end", "case a of b -> c."),
    exprs("missing arrow", "if a b end."),
    exprs("fun arity variable local", "fun f/N."),
    exprs("fun arity not integer", "fun m:f/a."),
    exprs("try without catch", "try a end."),
    exprs("receive nothing", "receive end."),
    exprs("maybe else nothing", "maybe a else end."),
    exprs("bad bit type", "<<X/1>>."),
    exprs("type after bit type", "<<X/integer:8>>."),
    exprs("dot inside", "a. b."),
    exprs("double dot", "a.."),
    exprs("operator only", "*."),
    exprs("comma at start", ", a."),
    exprs("double comma", "f(a,, b)."),
    exprs("trailing comma", "[a, b, ]."),
    exprs("record without fields", "#r."),
    exprs("record bad field", "#r{a}."),
    exprs("record field value", "#r{1 = a}."),
    exprs("map bad field", "#{a}."),
    exprs("map arrow", "#{a -> b}."),
    exprs("named fun mismatch syntax", "fun F() -> a; () -> b end."),
    exprs("catch class pattern", "try a catch (x):y -> z end."),
    exprs("dangling when", "case a of b when -> c end."),
    exprs("after without body", "receive after 1 end."),
    form("type missing parens", "-type t :: a."),
    form("spec without arrow", "-spec f()."),
    form("spec bad argument", "-spec f(1 +) -> a."),
    form("type annotated atom", "-type t() :: a :: b."),
    form("type double range", "-type t() :: 1..2..3."),
    form("type remote without call", "-type t() :: m:t."),
    form("type list comma", "-type t() :: [a, b]."),
    form("type bad map field", "-type t() :: #{a}."),
    form("type map trailing comma", "-type t() :: #{a => b,}."),
    form("type record field", "-type t() :: #r{a}."),
    form("type binary", "-type t() :: <<_:a>>."),
    form("record field typed later", "-record(r, {a, b}, {c :: d})."),
    form("typed record expression", "-record(r, {a :: b} ++ c)."),
    form("attribute paren no close", "-foo(a, b."),
    form("record with call", "-record(r(), {a})."),
    form("spec no clause", "-spec f."),
    form("spec module no name", "-spec m:() -> ok."),
    form("record native no fields", "-record #r."),
    form("record nothing", "-record."),
    form("attribute without name", "-(a)."),
    form("attribute variable name", "-X(a)."),
    form("binary type unit order", "-type t() :: <<_:_*8, _:4>>."),
    form("unit type", "-type t() :: <<_:A*8>>."),
    form("spec any arguments", "-spec f(...) -> a."),
    form("import one value", "-import(lists)."),
    form("module three values", "-module(m, x, y).").

action_errors() ->
    form("export bad arity", "-export([f/a])."),
    form("export bad name", "-export([1/2])."),
    form("export not a list", "-export(f)."),
    form("export bad element", "-export([f])."),
    form("export two values", "-export([f/1], [g/2])."),
    form("import two values", "-import(m, [f/1], x)."),
    form("import bad module", "-import(\"m\", [f/1])."),
    form("import_record bad name", "-import_record(m, [\"a\"])."),
    form("module string", "-module(\"m\")."),
    form("module bad variables", "-module(m, [a])."),
    form("module improper variables", "-module(m, [A | B])."),
    form("record no fields", "-record(r)."),
    form("record unparenthesized no fields", "-record r."),
    form("native record no fields", "-record #r."),
    form("record name then brace", "-record r{a}."),
    form("record name then hash", "-record record #r{a}."),
    form("native record then brace", "-record #r}."),
    form("native record parenthesized then brace", "-record(#r})."),
    form("record parenthesized then brace", "-record(r{a})."),
    form("record bad field", "-record(r, {a + b})."),
    form("record not a tuple", "-record(r, [a])."),
    form("record bad typed field", "-record(r, {1 :: integer()})."),
    form("record bad name", "-record(\"r\", {a})."),
    form("record three values", "-record(r, {a}, b)."),
    form("native_record attribute", "-native_record(a)."),
    form("file bad name", "-file(a, 1)."),
    form("file one value", "-file(\"a\")."),
    form("attribute two values", "-foo(a, b)."),
    form("attribute variable", "-foo(X)."),
    form("attribute call", "-foo(f())."),
    form("attribute map exact", "-foo(#{a := 1})."),
    form("attribute bad binary", "-foo(<<X>>)."),
    form("type bad variable", "-type t(1) :: a."),
    form("type underscore variable", "-type t(_) :: a."),
    form("type atom head", "-type t :: a."),
    form("opaque atom head", "-opaque t :: a."),
    form("nominal atom head", "-nominal t :: a."),
    form("other typed attribute", "-foo t() :: a."),
    form("record typed attribute", "-record r() :: a."),
    form("type typed fields", "-type t, {a :: b}."),
    form("foo typed fields", "-foo t, {a :: b}."),
    form("spec unsupported constraint", "-spec f(A) -> A when foo(A)."),
    form("spec bad type variable", "-spec f(A) -> A when _ :: a."),
    form("spec is_subtype bad variable", "-spec f(A) -> A when is_subtype(a, b)."),
    form("spec is_subtype three", "-spec f(A) -> A when is_subtype(A, b, c)."),
    form("bad binary type", "-type t() :: <<A:8>>."),
    form("bad binary unit type", "-type t() :: <<_:_*A>>."),
    form("bad binary unit base", "-type t() :: <<A:_*8>>."),
    form("doc bad value", "-doc 1."),
    form("doc bad map", "-doc #{a => X}."),
    form("doc bad map exact", "-doc #{a := 1}."),
    form("doc two values", "-doc(\"a\", \"b\")."),
    form("doc binary of variable", "-doc <<X>>."),
    form("doc bits", "-doc <<1:3>>."),
    form("doc map equiv not a call", "-doc #{equiv => a}."),
    form("head mismatch arity", "f() -> a; f(X) -> b."),
    form("head mismatch name", "f() -> a; g() -> b."),
    form("head mismatch before syntax error", "f() -> a; g() -> b x."),
    exprs("fun head mismatch", "fun () -> a; (X) -> b end."),
    exprs("named fun head mismatch", "fun F() -> a; G() -> b end."),
    exprs("named fun arity mismatch", "fun F() -> a; F(X) -> b end."),
    exprs("anonymous then named", "fun () -> a; F() -> b end."),
    exprs("illegal sigil prefix", "~x\"s\"."),
    exprs("illegal sigil suffix", "~s\"a\"x."),
    exprs("illegal binary sigil suffix", "~b\"a\"x."),
    exprs("illegal default sigil suffix", "~\"a\"x."),
    exprs("anonymous then named zip", "fun ({a}) -> b; F() -> c end."),
    exprs("filter then zip", "[a || b, c && d]."),
    exprs("two zips", "[a || b && c, d && e]."),
    exprs("bit types order", "<<X/integer-little-unit:8>>.").

texts() ->
    {ok, Tokens, _} = erl_scan:string("f(X) -> 'a b' c.", 1, [text]),
    show("text error", outcome(erl_parse:parse_form(Tokens))),
    {ok, Good, _} = erl_scan:string("f(X) -> [X | 'a b'].", 1, [text]),
    show("text form", erl_parse:parse_form(Good)),
    {ok, Open, _} = erl_scan:string("f(X) ->\n  g(X", {1, 1}, [text]),
    show("text end", outcome(erl_parse:parse_form(Open))),
    {ok, Lines, _} = erl_scan:string("f(X) ->\n  g(X", 1),
    show("line end", outcome(erl_parse:parse_form(Lines))),
    {ok, Plain, _} = erl_scan:string("f() -> a.\ng() ->\n b."),
    show("lines only", erl_parse:parse_form(Plain)),
    {ok, Strs, _} = erl_scan:string("f() -> \"a\" \"b\" $c 1.5 X 'q'.", 1, [text]),
    show(
        "text string error", outcome(erl_parse:parse_form(lists:droplast(Strs) ++ [{atom, 1, z}]))
    ),
    show("token error string", outcome(erl_parse:parse_exprs([{string, 3, "x"}, {dot, 3}]))),
    show(
        "token error char", outcome(erl_parse:parse_exprs([{atom, 1, a}, {char, 2, $x}, {dot, 2}]))
    ),
    show(
        "token error float",
        outcome(erl_parse:parse_exprs([{atom, 1, a}, {float, 2, 1.5}, {dot, 2}]))
    ),
    show(
        "token error integer",
        outcome(erl_parse:parse_exprs([{atom, 1, a}, {integer, 2, 7}, {dot, 2}]))
    ),
    show(
        "token error var", outcome(erl_parse:parse_exprs([{atom, 1, a}, {var, 2, 'V'}, {dot, 2}]))
    ),
    show(
        "token error quoted atom",
        outcome(erl_parse:parse_exprs([{atom, 1, a}, {atom, 2, 'x y'}, {dot, 2}]))
    ),
    show("token error symbol", outcome(erl_parse:parse_exprs([{atom, 1, a}, {'(', 2}]))),
    show("token error dot", outcome(erl_parse:parse_exprs([{'(', 1}, {dot, 2}]))),
    show(
        "token error unknown",
        outcome(erl_parse:parse_exprs([{atom, 1, a}, {foo, 2, bar}, {dot, 2}]))
    ),
    show(
        "token error unknown pair",
        outcome(erl_parse:parse_exprs([{atom, 1, a}, {foo, 2}, {dot, 2}]))
    ),
    show("empty form", outcome(erl_parse:parse_form([]))),
    show("empty term", outcome(erl_parse:parse_term([]))),
    show(
        "anno terms",
        erl_parse:parse_exprs([{atom, {7, 3}, a}, {'+', 9}, {integer, [{location, 4}], 1}, {dot, 0}])
    ),
    show("odd annotations", erl_parse:parse_exprs([{atom, anno, a}, {dot, x}])),
    show(
        "spec keyword token",
        erl_parse:parse_form([
            {'-', 1},
            {spec, 1},
            {atom, 1, f},
            {'(', 1},
            {')', 1},
            {'->', 1},
            {atom, 1, ok},
            {dot, 1}
        ])
    ).

abstract() ->
    show("atom", erl_parse:abstract(foo)),
    show("string", erl_parse:abstract("abc")),
    show("integers", erl_parse:abstract([1, 2, 3])),
    show("mixed list", erl_parse:abstract([1, a])),
    show("string then atom", erl_parse:abstract([97, 98, c])),
    show("negative", erl_parse:abstract([-1, 2])),
    show("improper", erl_parse:abstract([97 | b])),
    show("improper integer tail", erl_parse:abstract([97 | 98])),
    show("tuple", erl_parse:abstract({a, 1.5, {}})),
    show("map", erl_parse:abstract(#{a => [x]})),
    show("integer map", erl_parse:abstract(#{1 => a, 2 => [x]})),
    show("binary", erl_parse:abstract(<<1, 2>>)),
    show("bits", erl_parse:abstract(<<1, 2:3>>)),
    show("empty binary", erl_parse:abstract(<<>>)),
    show("fun", erl_parse:abstract(fun lists:map/2)),
    show("nested", erl_parse:abstract([{a, "b"}, [], [[]]])),
    show("unicode", erl_parse:abstract([16#1F600, 97])),
    show("latin1", erl_parse:abstract([256, 97], [{encoding, latin1}])),
    show("latin1 string", erl_parse:abstract([255, 97], [{encoding, latin1}])),
    show("none", erl_parse:abstract("ab", [{encoding, none}])),
    show("unicode encoding", erl_parse:abstract([16#D800, 97], [{encoding, unicode}])),
    show("utf8 encoding", erl_parse:abstract([16#10FFFF], [{encoding, utf8}])),
    show("encoding fun", erl_parse:abstract("abc", [{encoding, fun(C) -> C < 99 end}])),
    show("encoding fun all", erl_parse:abstract("ab", [{encoding, fun(_) -> true end}])),
    show("line", erl_parse:abstract(x, 5)),
    show("location", erl_parse:abstract([a], {3, 4})),
    show("line option", erl_parse:abstract(x, [{line, 7}])),
    show("location option", erl_parse:abstract(x, [{location, {2, 3}}, {line, 9}])),
    show("empty options", erl_parse:abstract("a", [])),
    show("big integer", erl_parse:abstract(123456789012345678901234567890)),
    show("bad encoding", catch_error(fun() -> erl_parse:abstract("a", [{encoding, foo}]) end)),
    show("local fun", catch_error(fun() -> erl_parse:abstract(fun() -> ok end) end)),
    show("pid", catch_error(fun() -> erl_parse:abstract(self()) end)).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

normalise() ->
    N = fun erl_parse:normalise/1,
    show("normalise atom", N({atom, 1, a})),
    show("normalise compound", N(erl_parse:abstract({a, [1, "s"], #{k => <<1, 2>>}}))),
    show(
        "normalise negatives",
        N(
            {cons, 1, {op, 1, '-', {integer, 1, 3}},
                {cons, 1, {op, 1, '-', {char, 1, $a}}, {nil, 1}}}
        )
    ),
    show(
        "normalise plus",
        N({tuple, 1, [{op, 1, '+', {float, 1, 1.5}}, {op, 1, '+', {char, 1, $b}}]})
    ),
    show(
        "normalise fun",
        N({'fun', 1, {function, {atom, 1, lists}, {atom, 1, map}, {integer, 1, 2}}})
    ),
    show("normalise string", N({string, 1, "abc"})),
    show("normalise bits", N(erl_parse:abstract(<<1, 2:3>>))),
    show(
        "normalise binary string",
        N({bin, 1, [{bin_element, 1, {string, 1, "ab"}, default, default}]})
    ),
    show("normalise bad", catch_error(fun() -> N({var, 1, 'X'}) end)),
    show(
        "normalise bad map",
        catch_error(fun() -> N({map, 1, [{map_field_exact, 1, {atom, 1, a}, {atom, 1, b}}]}) end)
    ),
    show("normalise bad op", catch_error(fun() -> N({op, 1, '-', {atom, 1, a}}) end)),
    show(
        "normalise bad binary",
        catch_error(fun() -> N({bin, 1, [{bin_element, 1, {var, 1, 'X'}, default, default}]}) end)
    ),
    show("normalise improper", N({cons, 1, {atom, 1, a}, {atom, 1, b}})),
    Segment = fun(Value, Size, Types) ->
        catch_error(fun() -> N({bin, 1, [{bin_element, 1, Value, Size, Types}]}) end)
    end,
    One = {integer, 1, 1},
    show("normalise unknown bit type", Segment(One, default, [foo])),
    show("normalise bit type mismatch", Segment(One, default, [integer, float])),
    show("normalise endianness mismatch", Segment(One, default, [native, little])),
    show("normalise unit mismatch", Segment({bin, 1, []}, default, [bytes, bits])),
    show("normalise unit without size", Segment(One, default, [{unit, 8}])),
    show("normalise bad unit", Segment(One, {integer, 1, 8}, [{unit, 300}])),
    show("normalise utf size", Segment({integer, 1, 97}, {integer, 1, 8}, [utf8])),
    show("normalise utf unit", Segment({integer, 1, 97}, default, [utf16, {unit, 1}])),
    show("normalise bad value", Segment({atom, 1, a}, default, default)),
    show(
        "normalise binary unit",
        Segment({bin, 1, [{bin_element, 1, One, {integer, 1, 3}, default}]}, default, [
            {unit, 2}, binary
        ])
    ),
    show("normalise native", Segment({integer, 1, 258}, {integer, 1, 16}, [native])),
    show("normalise string types", Segment({string, 1, "ab"}, {integer, 1, 16}, [little])),
    show("normalise empty string types", Segment({string, 1, ""}, default, [foo])).

tokens() ->
    show("tokens", erl_parse:tokens(erl_parse:abstract({a, [1, "s"], #{k => v}, [x | y]}))),
    show("tokens more", erl_parse:tokens({nil, 1}, [{dot, 1}])),
    show(
        "tokens atoms",
        erl_parse:tokens({tuple, 2, [{var, 3, 'X'}, {char, 4, $c}, {float, 5, 1.0}]})
    ),
    show("tokens empty", erl_parse:tokens({tuple, 1, []}, [])),
    show("tokens map", erl_parse:tokens({map, 1, []})),
    show("tokens bad", catch_error(fun() -> erl_parse:tokens({bin, 1, []}) end)).

precedences() ->
    Ops = [
        '=',
        '!',
        'orelse',
        'andalso',
        '==',
        '/=',
        '=<',
        '<',
        '>=',
        '>',
        '=:=',
        '=/=',
        '++',
        '--',
        '+',
        '-',
        'bor',
        'bxor',
        'bsl',
        'bsr',
        'or',
        'xor',
        '*',
        '/',
        'div',
        'rem',
        'band',
        'and',
        '#',
        '(',
        ':',
        '.'
    ],
    show("inop_prec", [erl_parse:inop_prec(Op) || Op <- Ops]),
    show("preop_prec", [erl_parse:preop_prec(Op) || Op <- ['catch', '+', '-', 'bnot', 'not', '#']]),
    show("func_prec", erl_parse:func_prec()),
    show("max_prec", erl_parse:max_prec()),
    TypeOps = [
        '=',
        '::',
        '|',
        '..',
        '+',
        '-',
        'bor',
        'bxor',
        'bsl',
        'bsr',
        '*',
        '/',
        'div',
        'rem',
        'band',
        '#'
    ],
    show("type_inop_prec", [erl_parse:type_inop_prec(Op) || Op <- TypeOps]),
    show("type_preop_prec", [erl_parse:type_preop_prec(Op) || Op <- ['+', '-', 'bnot', '#']]),
    show("inop_prec bad", catch_error(fun() -> erl_parse:inop_prec(foo) end)).

annotations() ->
    {ok, Form} = erl_parse:parse_form(tokens_of("f(X) -> #r{a = X}, [X | x].")),
    show("form for annotations", Form),
    {ok, Spec} = erl_parse:parse_form(tokens_of("-spec f(A) -> A when A :: atom().")),
    {ok, Type} = erl_parse:parse_form(tokens_of("-type t(A) :: {A, #r{a :: b}}.")),
    {ok, Record} = erl_parse:parse_form(tokens_of("-record(r, {a = 1 :: integer()}).")),
    {ok, Fun} = erl_parse:parse_form(
        tokens_of("f() -> fun m:f/1, fun g/2, fun (X) -> X end, X#r.a.")
    ),
    Forms = [Form, Spec, Type, Record, Fun, {eof, 3}, {error, x}, {warning, y}],
    show("map_anno", erl_parse:map_anno(fun({L, C}) -> L * 100 + C end, Forms)),
    show("fold_anno", erl_parse:fold_anno(fun(A, Acc) -> [A | Acc] end, [], Forms)),
    show("mapfold_anno", erl_parse:mapfold_anno(fun(_, N) -> {N, N + 1} end, 0, Forms)),
    show("new_anno", erl_parse:new_anno({cons, 3, {atom, {3, 4}, a}, {nil, 5}})),
    show("anno_to_term", erl_parse:anno_to_term({atom, erl_anno:set_generated(true, 3), a})),
    show("anno_from_term", erl_parse:anno_from_term({atom, [{location, 3}, {generated, true}], a})),
    show(
        "first_anno",
        erl_parse:first_anno(
            {call, {1, 5}, {remote, {1, 2}, {atom, {1, 1}, m}, {atom, {1, 3}, f}}, []}
        )
    ),
    show(
        "map_anno bad",
        catch_error(fun() -> erl_parse:map_anno(fun(A) -> A end, {a, b, c, d, e, f, g}) end)
    ).

%% yecc's parse/1, parse_and_scan/1 and format_error/1.
yecc() ->
    show("parse", erl_parse:parse(tokens_of("-spec(f) -> ok."))),
    Chunks = fun(Strings) ->
        [{ok, tokens_of(String), 1} || String <- Strings] ++ [{eof, {9, 9}}]
    end,
    scan_chunks("chunks", Chunks(["f() ->", " ok."])),
    scan_chunks("chunks with empty", Chunks(["f()", "", " -> ok."])),
    scan_chunks("chunks end early", Chunks(["f() ->"])),
    scan_chunks("chunks no tokens", Chunks([])),
    scan_chunks("chunks after dot", Chunks(["f() -> ok.", "g"])),
    scan_chunks("chunks syntax error", Chunks(["f() -> )", " ok."])),
    scan_chunks("chunks action error", Chunks(["-export(f).", ""])),
    scan_chunks("chunks scan error", [{ok, tokens_of("f() ->"), 1}, {error, {1, erl_scan, x}, 2}]),
    put(chunks, Chunks(["-module(", "m)."])),
    show("parse_and_scan mfa", erl_parse:parse_and_scan({?MODULE, next_chunk, []})),
    show("format_error string", erl_parse:format_error(["syntax error before: ", "x"])),
    show("format_error term", lists:flatten(erl_parse:format_error({bad, [1]}))).

scan_chunks(Label, Chunks) ->
    put(chunks, Chunks),
    show(Label, guarded(fun() -> erl_parse:parse_and_scan({fun next_chunk/0, []}) end)).

next_chunk() ->
    case get(chunks) of
        [Chunk | Rest] ->
            put(chunks, Rest),
            Chunk;
        [] ->
            {eof, 99}
    end.
