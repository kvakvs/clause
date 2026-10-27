%% Clause's module interpreter (plan 21 step LIB77B, docs/libraries/library.md#module-interpreter): runs a module from
%% its abstract forms over erl_eval, so a program can use modules that were not compiled into it (the transform
%% host's project transforms). load/1 checks the forms with erl_lint, expands records, adds module_info/0,1 and
%% behaviour_info/1 and registers the module with the runtime: its calls from compiled code (M:F(...), apply/3,
%% fun M:F/A, spawn MFAs) then enter error_handler:undefined_function/3, which calls call/3 here, and code queries
%% and erlang:function_exported/3 see it as loaded.
%%
%% Each function is an erl_eval fun of its argument list. Local calls become remote calls of the module
%% '$clause_interp_local', and calls in tail position get their module wrapped in {'$clause_interp_tail', Module},
%% so the non-local function handler knows both. The handler keeps a shadow stack of the interpreted functions
%% running in the process dictionary, with the line each is at, and exceptions leaving interpreted code get OTP-like
%% frames {Module, Function, Arity, [{file, File}, {line, Line}]} for them in place of erl_eval's.
%%
%% With a path (set_path/1, plan 21 LIB77C) every undefined call enters error_handler, which interprets the module
%% from the debug_info chunk of its .beam on the path when it has one (load_from_path/1).
%%
%% compile's binaries are interpreter images (plan 21 LIB77E1): beam/2 wraps linted forms in a BEAM container,
%% load_binary/3 interprets one for code:load_binary/3 and the other loading functions of code.
-module(clause_interp).
-export([load/1, unload/1, call/3, format_error/1, set_path/1, ensure_loaded/1, load_from_path/1]).
-export([beam/2, load_binary/3]).
-export([register/4, unregister/1, interpreted/1, route_undefined/1]).

-define(LOCAL, '$clause_interp_local').
-define(TAIL, '$clause_interp_tail').
-define(NO_CLAUSE, '$clause_interp_no_clause').
-define(STACK, '$clause_interp_stack').
-define(RAISED, '$clause_interp_raised').
%% Frames of these modules are the interpreter's own; exceptions show the interpreted frames instead.
-define(INTERNAL, [erl_eval, clause_erl_eval, clause_interp, error_handler, prim_eval, clause_receive]).
%% OTP's backtrace depth.
-define(DEPTH, 8).
%% The chunk of an interpreter image that holds its forms (plan 21 LIB77E1).
-define(FORMS_CHUNK, "ClFm").

%% An interpreted module: its file, functions {Name, Arity} => #function{}, exports and what module_info/1 tells.
-record(image, {module, file, functions = #{}, exports = [], attributes = [], md5, compile = []}).
%% One function: the erl_eval fun of its argument list, and the file and line of its definition.
-record(function, {fun_value, file, line}).
%% What rewriting a module's code needs: its name, the functions it defines and the ones it imports.
-record(context, {module, locals, imports}).

%% Runtime builtins (runtime/src/builtins/interpreted.cpp).

%% register(Module, File, Exports, Route): calls of Module enter Route from now on; linked when the program has Module.
register(_, _, _, _) -> clause_builtin_placeholder.

%% unregister(Module): true when Module was interpreted.
unregister(_) -> clause_builtin_placeholder.

%% interpreted(Module): whether Module is interpreted.
interpreted(_) -> clause_builtin_placeholder.

%% route_undefined(Route): every call of an undefined function enters Route from now on (set_path/1).
route_undefined(_) -> clause_builtin_placeholder.

%% Loading

%% load(Forms): {ok, Module, Warnings} after interpreting the module of Forms (replacing an interpreted one of that
%% name), or {error, Errors, Warnings} as compile:forms/2 returns them.
load(Forms) ->
    load(Forms, source_file(Forms)).

%% Interpret Forms as loaded from Location, the file code queries show.
load(Forms, Location) ->
    File = source_file(Forms),
    case erl_lint:module(Forms, File, []) of
        {ok, Warnings} -> install(Forms, File, Location, Warnings, [{options, []}, {source, File}]);
        {error, Errors, Warnings} -> {error, Errors, Warnings}
    end.

%% unload(Module): stop interpreting Module; false when it was not interpreted.
unload(Module) ->
    _ = persistent_term:erase({?MODULE, Module}),
    clause_interp:unregister(Module).

format_error({linked, Module}) ->
    io_lib:format("module '~ts' is part of the program and cannot be interpreted", [Module]).

%% Interpret linted forms; Compile is what module_info(compile) tells.
install(Forms0, File, Location, Warnings, Compile) ->
    Forms = erl_internal:add_predefined_functions(erl_expand_records:module(Forms0, [])),
    Image = (image(Forms, File))#image{md5 = md5(Forms0), compile = Compile},
    Module = Image#image.module,
    persistent_term:put({?MODULE, Module}, Image),
    case clause_interp:register(Module, Location, Image#image.exports, fun error_handler:undefined_function/3) of
        ok ->
            {ok, Module, Warnings};
        linked ->
            _ = persistent_term:erase({?MODULE, Module}),
            {error, [{File, [{none, ?MODULE, {linked, Module}}]}], Warnings}
    end.

%% Interpreter images (plan 21 LIB77E1)

%% beam(Forms, Chunks): the image of linted Forms that compile returns: a BEAM container with the module's atom
%% chunk, Attr as OTP's (attributes with vsn), Chunks (compile's CInf, Dbgi and extra chunks) and the forms.
beam(Forms, Chunks) ->
    Module = hd([M || {attribute, _, module, M} <- Forms]),
    Attributes = with_vsn(attributes(Forms), md5(Forms)),
    Head = [{"AtU8", atom_chunk(Module)}, {"Attr", term_to_binary(Attributes)}],
    beam_lib:build_module(Head ++ Chunks ++ [{?FORMS_CHUNK, term_to_binary(Forms)}]).

%% An atom chunk of one atom, its length in the compact encoding of OTP 28 and later.
atom_chunk(Module) ->
    Name = atom_to_binary(Module),
    Length =
        case byte_size(Name) of
            Small when Small < 16 -> <<Small:4, 0:4>>;
            Large -> <<(Large bsr 8):3, 1:2, 0:3, Large:8>>
        end,
    <<-1:32/signed, Length/binary, Name/binary>>.

%% load_binary(Module, File, Binary): {module, Module} after interpreting an image of Module, File being what code
%% queries show; {error, badfile} for another binary, an image of another module or one of a linked module.
load_binary(Module, File, Binary) when is_atom(File) ->
    load_binary(Module, atom_to_list(File), Binary);
load_binary(Module, File, Binary) ->
    case image_chunks(Binary) of
        {ok, Module, Forms, Compile} -> loaded_image(install(Forms, source_file(Forms), File, [], Compile));
        _ -> {error, badfile}
    end.

image_chunks(Binary) ->
    try beam_lib:chunks(Binary, [?FORMS_CHUNK, "CInf"]) of
        {ok, {Module, [{_, Forms}, {_, Compile}]}} -> {ok, Module, binary_to_term(Forms), binary_to_term(Compile)};
        {error, beam_lib, _} -> error
    catch
        _:_ -> error
    end.

loaded_image({ok, Module, _}) -> {module, Module};
loaded_image({error, _, _}) -> {error, badfile}.

%% Modules from .beam files (plan 21 LIB77C)

%% set_path(Directories): interpret modules from their .beam files in the directories when first called, or asked
%% for with ensure_loaded/1; an empty list changes nothing.
set_path([]) ->
    ok;
set_path(Directories) ->
    persistent_term:put({?MODULE, path}, Directories),
    clause_interp:route_undefined(fun error_handler:undefined_function/3).

%% ensure_loaded(Module): {module, Module} for a module the program links or interprets, or interprets now from its
%% .beam on the path; {error, nofile} without one, {error, {Reason, File}} when its .beam cannot be interpreted.
ensure_loaded(Module) ->
    case code:ensure_loaded(Module) of
        {module, Module} -> {module, Module};
        {error, _} -> load_beam(Module)
    end.

%% load_from_path(Module): whether Module, neither linked nor interpreted, is interpreted now from its .beam.
load_from_path(Module) ->
    not erlang:module_loaded(Module) andalso load_beam(Module) =:= {module, Module}.

load_beam(Module) ->
    Name = atom_to_list(Module) ++ ".beam",
    Path = persistent_term:get({?MODULE, path}, []),
    case [File || Directory <- Path, File <- [filename:join(Directory, Name)], filelib:is_regular(File)] of
        [File | _] -> interpret_beam(Module, File);
        [] -> {error, nofile}
    end.

%% A .beam compile wrote is an image (plan 21 LIB77E1); OTP's need debug_info.
interpret_beam(Module, File) ->
    case file:read_file(File) of
        {ok, Binary} -> interpret_beam(Module, File, Binary);
        {error, Reason} -> {error, {Reason, File}}
    end.

interpret_beam(Module, File, Binary) ->
    case load_binary(Module, File, Binary) of
        {module, Module} -> {module, Module};
        {error, badfile} -> interpret_debug_info(Module, File)
    end.

interpret_debug_info(Module, File) ->
    case beam_lib:chunks(File, [abstract_code]) of
        {ok, {Module, [{abstract_code, {raw_abstract_v1, Forms}}]}} -> loaded_beam(Module, File, load(Forms, File));
        {ok, {Module, [{abstract_code, no_abstract_code}]}} -> {error, {no_debug_info, File}};
        {ok, {Other, _}} -> {error, {{module_name, Other}, File}};
        {error, beam_lib, Reason} -> {error, {Reason, File}}
    end.

loaded_beam(Module, _File, {ok, Module, _Warnings}) -> {module, Module};
loaded_beam(_Module, File, {error, Errors, _Warnings}) -> {error, {Errors, File}}.

%% The file of the forms: the first file attribute's.
source_file([{attribute, _, file, {File, _}} | _]) -> File;
source_file([_ | Forms]) -> source_file(Forms);
source_file([]) -> "".

image(Forms, File) ->
    Module = hd([M || {attribute, _, module, M} <- Forms]),
    Defined = [{Name, Arity} || {function, _, Name, Arity, _} <- Forms],
    Imports = maps:from_list([{FA, M} || {attribute, _, import, {M, FAs}} <- Forms, FA <- FAs]),
    Context = #context{module = Module, locals = sets:from_list(Defined), imports = Imports},
    #image{
        module = Module,
        file = File,
        functions = functions(Forms, File, Context, #{}),
        exports = exports(Forms, Defined),
        attributes = attributes(Forms)
    }.

md5(Forms) -> erlang:md5(term_to_binary(Forms)).

%% Every function of the forms, each from the file that the file attributes before it name.
functions([{attribute, _, file, {File, _}} | Forms], _File, Context, Acc) ->
    functions(Forms, File, Context, Acc);
functions([{function, Anno, Name, Arity, Clauses} | Forms], File, Context, Acc) ->
    Function = #function{
        fun_value = function_fun(Anno, Clauses, Context),
        file = File,
        line = erl_anno:line(Anno)
    },
    functions(Forms, File, Context, Acc#{{Name, Arity} => Function});
functions([_ | Forms], File, Context, Acc) ->
    functions(Forms, File, Context, Acc);
functions([], _File, _Context, Acc) ->
    Acc.

%% The export attributes' functions, or every function under export_all.
exports(Forms, Defined) ->
    Options = lists:flatten([Option || {attribute, _, compile, Option} <- Forms]),
    case lists:member(export_all, Options) of
        true -> Defined;
        false -> lists:usort(lists:append([FAs || {attribute, _, export, FAs} <- Forms]))
    end.

%% The attributes module_info(attributes) shows: the ones the compiler does not consume, values as lists.
attributes(Forms) ->
    Consumed = [
        module,
        export,
        import,
        export_type,
        file,
        record,
        type,
        opaque,
        spec,
        callback,
        optional_callbacks,
        compile,
        nifs,
        on_load,
        feature
    ],
    [
        {Name, as_list(Value)}
     || {attribute, _, Name, Value} <- Forms, not lists:member(Name, Consumed)
    ].

as_list(Value) when is_list(Value) -> Value;
as_list(Value) -> [Value].

%% The attributes with OTP's vsn, the module's MD5 as an integer, unless the module gives one.
with_vsn(Attributes, Md5) ->
    case lists:keymember(vsn, 1, Attributes) of
        true -> Attributes;
        false -> [{vsn, [binary:decode_unsigned(Md5)]} | Attributes]
    end.

%% A function as an erl_eval fun of its argument list; a last clause raises function_clause for the handler.
function_fun(Anno, Clauses, Context) ->
    Line = {integer, Anno, erl_anno:line(Anno)},
    Rest = {var, Anno, '_ClauseInterpArguments'},
    NoClause = {call, Anno, {remote, Anno, {atom, Anno, ?LOCAL}, {atom, Anno, ?NO_CLAUSE}}, [Line, Rest]},
    All = [list_clause(Clause, Context) || Clause <- Clauses] ++ [{clause, Anno, [Rest], [], [NoClause]}],
    Ef = {value, fun(A, Spec, As) -> nonlocal(Context#context.module, A, Spec, As) end},
    erl_eval:expr({'fun', Anno, {clauses, All}}, erl_eval:new_bindings(), none, Ef, value).

%% A function clause taking its arguments as one list.
list_clause({clause, Anno, Patterns, Guards, Body}, Context) ->
    Head = lists:foldr(fun(P, Tail) -> {cons, Anno, P, Tail} end, {nil, Anno}, Patterns),
    {clause, Anno, [Head], Guards, body(Body, Context, true)}.

%% Rewriting code: local calls and fun Name/Arity reach the handler, tail calls are marked.

body([E], Context, Tail) -> [expr(E, Context, Tail)];
body([E | Es], Context, Tail) -> [expr(E, Context, false) | body(Es, Context, Tail)];
body([], _Context, _Tail) -> [].

clauses(Clauses, Context, Tail) ->
    [{clause, A, H, G, body(B, Context, Tail)} || {clause, A, H, G, B} <- Clauses].

expr({call, A, {atom, _, Name} = Atom, As0}, Context, Tail) ->
    As = exprs(As0, Context),
    case local_target(Name, length(As), Context) of
        bif -> {call, A, Atom, As};
        Target -> {call, A, {remote, A, marked({atom, A, Target}, Tail), Atom}, As}
    end;
expr({call, A, {remote, Ar, M, F}, As}, Context, Tail) ->
    Module = expr(M, Context, false),
    {call, A, {remote, Ar, marked(Module, Tail andalso not erlang_call(Module)), expr(F, Context, false)},
        exprs(As, Context)};
expr({'fun', A, {function, Name, Arity}} = Fun, Context, _Tail) when is_atom(Name) ->
    case local_target(Name, Arity, Context) of
        bif ->
            Fun;
        _ ->
            Vars = [{var, A, list_to_atom("_ClauseInterp" ++ integer_to_list(I))} || I <- lists:seq(1, Arity)],
            Call = expr({call, A, {atom, A, Name}, Vars}, Context, false),
            {'fun', A, {clauses, [{clause, A, Vars, [], [Call]}]}}
    end;
expr({'fun', A, {clauses, Cs}}, Context, _Tail) ->
    {'fun', A, {clauses, clauses(Cs, Context, false)}};
expr({named_fun, A, Name, Cs}, Context, _Tail) ->
    {named_fun, A, Name, clauses(Cs, Context, false)};
expr({block, A, B}, Context, Tail) ->
    {block, A, body(B, Context, Tail)};
expr({'case', A, E, Cs}, Context, Tail) ->
    {'case', A, expr(E, Context, false), clauses(Cs, Context, Tail)};
expr({'if', A, Cs}, Context, Tail) ->
    {'if', A, clauses(Cs, Context, Tail)};
expr({'receive', A, Cs}, Context, Tail) ->
    {'receive', A, clauses(Cs, Context, Tail)};
expr({'receive', A, Cs, T, B}, Context, Tail) ->
    {'receive', A, clauses(Cs, Context, Tail), expr(T, Context, false), body(B, Context, Tail)};
expr({'try', A, B, Cs, Hs, []}, Context, Tail) ->
    {'try', A, body(B, Context, false), clauses(Cs, Context, Tail), clauses(Hs, Context, Tail), []};
expr({'try', A, B, Cs, Hs, After}, Context, _Tail) ->
    {'try', A, body(B, Context, false), clauses(Cs, Context, false), clauses(Hs, Context, false),
        body(After, Context, false)};
expr({clause, A, H, G, B}, Context, _Tail) ->
    {clause, A, H, G, body(B, Context, false)};
expr(E, Context, _Tail) when is_tuple(E) ->
    list_to_tuple(exprs(tuple_to_list(E), Context));
expr(E, Context, _Tail) when is_list(E) ->
    exprs(E, Context);
expr(E, _Context, _Tail) ->
    E.

exprs(Es, Context) -> [expr(E, Context, false) || E <- Es].

%% Where a call of Name/Arity without a module goes: this module, an imported module, or an auto-imported BIF.
local_target(Name, Arity, #context{locals = Locals, imports = Imports}) ->
    case sets:is_element({Name, Arity}, Locals) of
        true -> ?LOCAL;
        false -> maps:get({Name, Arity}, Imports, bif)
    end.

%% erlang's functions are BIFs: their frames stay as in compiled code, so calls of them are never tail calls.
erlang_call({atom, _, erlang}) -> true;
erlang_call(_) -> false.

marked(Module, true) -> {tuple, element(2, Module), [{atom, element(2, Module), ?TAIL}, Module]};
marked(Module, false) -> Module.

%% Calls

%% call(Module, Function, Args): a call of interpreted Module's export from compiled code
%% (error_handler:undefined_function/3); undef when Module does not export it.
call(Module, Function, Args) ->
    case exported_function(Module, Function, length(Args)) of
        {ok, Entry, Fun} -> boundary(Entry, Fun, Args);
        error -> outside_undef(Module, Function, Args)
    end.

exported_function(Module, Name, Arity) ->
    case persistent_term:get({?MODULE, Module}, none) of
        #image{exports = Exports} = Image ->
            case lists:member({Name, Arity}, Exports) of
                true -> function(Image, Name, Arity);
                false -> error
            end;
        none ->
            error
    end.

function(#image{module = Module, functions = Functions}, Name, Arity) ->
    case Functions of
        #{{Name, Arity} := #function{fun_value = Fun, file = File, line = Line}} ->
            {ok, {Module, Name, Arity, File, Line}, Fun};
        #{} ->
            error
    end.

%% Entering interpreted code from compiled code: the outermost entry adds the compiled callers' frames.
boundary(Entry, Fun, Args) ->
    Saved = stack(),
    set_stack([Entry | Saved]),
    try Fun(Args) of
        Value ->
            set_stack(Saved),
            Value
    catch
        Class:Reason:Stack ->
            Unwound = unwind(Saved, Stack),
            erlang:raise(Class, Reason, outermost(Saved, Unwound))
    end.

outermost([], Stack) ->
    erase(?RAISED),
    lists:sublist(Stack ++ native(stacktrace()), ?DEPTH);
outermost(_Saved, Stack) ->
    Stack.

%% erl_eval's non-local function handler of Module's code: Spec is {Module, Function} or a fun.
nonlocal(Module, Anno, Spec, As) ->
    at_line(erl_anno:line(Anno)),
    case Spec of
        {{?TAIL, Target}, Name} -> dispatch(Module, Target, Name, As, tail);
        {Target, Name} -> dispatch(Module, Target, Name, As, body);
        Fun -> apply(Fun, As)
    end.

dispatch(_Module, ?LOCAL, ?NO_CLAUSE, [Line, Args], _Mode) ->
    [{M, F, _, File, _} | Rest] = stack(),
    raise(error, function_clause, [{M, F, Args, location(File, Line)} | frames(Rest)]);
dispatch(Module, ?LOCAL, Name, As, Mode) ->
    {ok, Entry, Fun} = function(persistent_term:get({?MODULE, Module}), Name, length(As)),
    enter(Entry, Fun, As, Mode);
dispatch(_Module, erlang, raise, [Class, Reason, Stack], _Mode) when is_list(Stack) ->
    case internal(Stack) of
        true -> raise(Class, Reason, evaluator_frames(Stack) ++ frames(stack()));
        false -> erlang:raise(Class, Reason, Stack)
    end;
dispatch(_Module, erlang, get_module_info, [Target | Item], Mode) ->
    case persistent_term:get({?MODULE, Target}, none) of
        none -> compiled(erlang, get_module_info, [Target | Item], Mode);
        Image -> module_info(Image, Item)
    end;
dispatch(_Module, Target, Name, As, Mode) when is_atom(Target) ->
    case persistent_term:get({?MODULE, Target}, none) of
        none ->
            compiled(Target, Name, As, Mode);
        _Image ->
            case exported_function(Target, Name, length(As)) of
                {ok, Entry, Fun} -> enter(Entry, Fun, As, Mode);
                error -> raise(error, undef, [{Target, Name, As, []} | frames(stack())])
            end
    end;
dispatch(_Module, Target, Name, As, Mode) ->
    compiled(Target, Name, As, Mode).

%% A call of compiled code: a tail call leaves the caller's frame. An exception it raises gets its interpreted frames
%% where the interpreter catches it next, from the shadow stack as it is now.
compiled(Module, Name, As, tail) ->
    set_stack(tl(stack())),
    apply(Module, Name, As);
compiled(Module, Name, As, body) ->
    apply(Module, Name, As).

%% A call of an interpreted function: a tail call replaces the caller's frame.
enter(Entry, Fun, As, tail) ->
    set_stack([Entry | tl(stack())]),
    Fun(As);
enter(Entry, Fun, As, body) ->
    Saved = stack(),
    set_stack([Entry | Saved]),
    try Fun(As) of
        Value ->
            set_stack(Saved),
            Value
    catch
        Class:Reason:Stack -> erlang:raise(Class, Reason, unwind(Saved, Stack))
    end.

%% Shadow stack and exception frames

stack() ->
    case get(?STACK) of
        undefined -> [];
        Stack -> Stack
    end.

set_stack([]) -> erase(?STACK);
set_stack(Stack) -> put(?STACK, Stack).

at_line(Line) ->
    case stack() of
        [{Module, Name, Arity, File, _} | Rest] -> put(?STACK, [{Module, Name, Arity, File, Line} | Rest]);
        [] -> ok
    end.

%% The stack of an exception leaving interpreted code, which then restores the shadow stack to Saved: compiled code's
%% frames on top, then the interpreted frames running when it was raised. A stack built that way already stays.
unwind(Saved, Stack) ->
    Unwound =
        case get(?RAISED) =:= Stack of
            true -> Stack;
            false -> marked_stack(native(Stack) ++ frames(stack()))
        end,
    set_stack(Saved),
    Unwound.

%% Raise with interpreted frames.
raise(Class, Reason, Stack) ->
    erlang:raise(Class, Reason, marked_stack(Stack)).

marked_stack(Stack) ->
    Trimmed = lists:sublist(Stack, ?DEPTH),
    put(?RAISED, Trimmed),
    Trimmed.

frames(Stack) ->
    [{Module, Name, Arity, location(File, Line)} || {Module, Name, Arity, File, Line} <- Stack].

location(File, Line) -> [{file, File}, {line, Line}].

%% The frames above the first of the interpreter's own.
native(Stack) ->
    lists:takewhile(fun(Frame) -> not lists:member(element(1, Frame), ?INTERNAL) end, Stack).

%% Whether erl_eval raised the error: its stack starts in the interpreter.
internal([Frame | _]) -> lists:member(element(1, Frame), ?INTERNAL);
internal([]) -> true.

%% erl_eval's frame of an interpreted fun without a matching clause stays; its other frames go.
evaluator_frames([{erl_eval, '-inside-an-interpreted-fun-', _} = Frame | _]) -> [Frame];
evaluator_frames(_) -> [].

stacktrace() ->
    try
        throw(stack)
    catch
        throw:stack:Stack -> Stack
    end.

%% The undef of a call from compiled code of a function the module does not export, as error_handler raises it.
outside_undef(Module, Function, Args) ->
    erlang:raise(error, undef, [{Module, Function, Args, []} | native(stacktrace())]).

%% module_info/0,1

module_info(Image, []) ->
    [{Key, module_info(Image, [Key])} || Key <- [module, exports, attributes, compile, md5]];
module_info(#image{module = Module}, [module]) ->
    Module;
module_info(#image{exports = Exports}, [exports]) ->
    Exports;
module_info(#image{functions = Functions}, [functions]) ->
    lists:sort(maps:keys(Functions));
module_info(#image{attributes = Attributes, md5 = Md5}, [attributes]) ->
    with_vsn(Attributes, Md5);
module_info(#image{compile = Compile}, [compile]) ->
    Compile;
module_info(#image{md5 = Md5}, [md5]) ->
    Md5;
module_info(_Image, [nifs]) ->
    [];
module_info(_Image, [native]) ->
    false;
module_info(_Image, [_Other]) ->
    erlang:error(badarg).
