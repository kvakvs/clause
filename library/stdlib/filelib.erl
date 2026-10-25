%% Clause's filelib (plan 21 step LIB51, docs/library.md): file name patterns and file system helpers, written from
%% OTP's documentation. Wildcard patterns are escaped, compiled part by part and matched against directory listings
%% by clause_filelib builtins (runtime/src/builtins/filelib.cpp); the directory walk, the queries and the searches
%% are Erlang over file, filename and re. The Mod argument of the undocumented forms names the module that answers
%% read_file_info/1, read_link_info/1 and list_dir/1 (file, erl_prim_loader or any other).
-module(filelib).
-export([
    compile_wildcard/1,
    ensure_dir/1,
    ensure_path/1,
    file_size/1,
    file_size/2,
    find_file/2,
    find_file/3,
    find_source/1,
    find_source/2,
    find_source/3,
    fold_files/5,
    fold_files/6,
    is_dir/1,
    is_dir/2,
    is_file/1,
    is_file/2,
    is_regular/1,
    is_regular/2,
    last_modified/1,
    last_modified/2,
    safe_relative_path/2,
    wildcard/1,
    wildcard/2,
    wildcard/3
]).
-include_lib("kernel/include/file.hrl").

% --- Wildcards ---

% The sorted names matching Pattern, relative to the working directory or Cwd.
wildcard(Pattern) when is_list(Pattern) -> matching(Pattern, ".", file).

wildcard(Pattern, Cwd) when is_list(Pattern), is_list(Cwd) -> matching(Pattern, Cwd, file);
wildcard(Pattern, Mod) when is_list(Pattern), is_atom(Mod) -> matching(Pattern, ".", Mod).

wildcard(Pattern, Cwd, Mod) when is_list(Pattern), is_list(Cwd), is_atom(Mod) -> matching(Pattern, Cwd, Mod).

% OTP's debugging aid shows its internal pattern form, which Clause's builtins do not share.
compile_wildcard(_) -> erlang:error({not_implemented, compile_wildcard, 1}).

matching(Pattern, Cwd, Mod) ->
    {Compiled, PrefixLength} = compile(Pattern, Cwd),
    Found = walk(Compiled, Mod),
    lists:sort([lists:nthtail(PrefixLength, Name) || Name <- Found]).

% The compiled pattern and the length of the Cwd prefix its names start with. The parts before the first one with a
% wildcard character are joined into one literal base, which is only checked to exist when nothing follows.
compile(Pattern, Cwd) ->
    [Root | Parts] = filename:split(clause_filelib:escape(Pattern)),
    case filename:pathtype(Root) of
        relative -> literal_prefix([Root | Parts], {cwd, base(Cwd)});
        _ -> literal_prefix(Parts, {root, 0, Root})
    end.

literal_prefix([Part | Parts], Base) ->
    Compiled = clause_filelib:compile_part(Part),
    case lists:all(fun erlang:is_integer/1, Compiled) of
        true -> literal_prefix(Parts, join_literal(Base, Compiled));
        false -> compiled([clause_filelib:compile_part(P) || P <- Parts], Compiled, Base)
    end;
literal_prefix([], {root, PrefixLength, Root}) ->
    {{exists, Root}, PrefixLength}.

compiled(Rest, First, {root, PrefixLength, Root}) -> {[Root, First | Rest], PrefixLength};
compiled(Rest, First, {cwd, Cwd}) -> {[Cwd, First | Rest], length(filename:join(Cwd, "x")) - 1}.

join_literal({cwd, "."}, Name) ->
    {root, 0, Name};
join_literal({cwd, Cwd}, Name) ->
    Relative = filename:join([Name]),
    Root = filename:join(Cwd, Relative),
    {root, length(Root) - length(Relative), Root};
join_literal({root, PrefixLength, Root}, Name) ->
    {root, PrefixLength, filename:join(Root, Name)}.

% A directory as a prefix for its entries' names: joined to "x" without the "x".
base(Dir) ->
    "x" ++ Reversed = lists:reverse(filename:join(Dir, "x")),
    lists:reverse(Reversed).

walk({exists, Name}, Mod) ->
    case exists(Name, Mod) of
        true -> [Name];
        false -> []
    end;
walk([Base | Parts], Mod) ->
    each([Base], Parts, [], Mod).

% The names below each of Bases that match Parts, added to Found.
each(Bases, Parts, Found, Mod) -> lists:foldl(fun(Base, Acc) -> below(Base, Parts, Acc, Mod) end, Found, Bases).

below(Base, [[double_star] | Parts], Found, Mod) ->
    double_star(".", [Base], Parts, Found, Mod, true);
below(Base, [".." | Parts], Found, Mod) ->
    case is_dir(Base, Mod) of
        true -> each([filename:join(Base, "..")], Parts, Found, Mod);
        false -> Found
    end;
below(Base, [Part | Parts], Found, Mod) ->
    case list_dir(Base, Mod) of
        {ok, Names} -> each(clause_filelib:matches(Part, base(Base), Names), Parts, Found, Mod);
        _ -> Found
    end;
below(Base, [], Found, _) ->
    [Base | Found].

% "**": every directory level below the root, each entry matched against the parts that follow (all entries when
% none follow); the root itself is never a result.
double_star(Dir, [Name | Names], Parts, Found, Mod, Root) ->
    Full =
        case Root of
            true -> Name;
            false -> filename:join(Dir, Name)
        end,
    Deeper =
        case list_dir(Full, Mod) of
            {ok, Entries} -> double_star(Full, Entries, Parts, Found, Mod, false);
            _ -> Found
        end,
    double_star(Dir, Names, Parts, double_star_entry(Root, Parts, Name, Full, Deeper, Mod), Mod, Root);
double_star(_, [], _, Found, _, _) ->
    Found.

double_star_entry(true, _, _, _, Found, _) ->
    Found;
double_star_entry(false, [], _, Full, Found, _) ->
    [Full | Found];
double_star_entry(false, [Part | Parts], Name, Full, Found, Mod) ->
    case clause_filelib:match(Part, Name) of
        true -> each([Full], Parts, Found, Mod);
        false -> Found
    end.

% Whether a literal name exists; on Windows "dir/.." also needs dir to be a directory, as OTP checks.
exists(Name, Mod) ->
    case read_link_info(Name, Mod) of
        {error, _} ->
            false;
        {ok, _} ->
            case os:type() of
                {win32, _} -> parents_are_directories(filename:split(Name), Mod, []);
                _ -> true
            end
    end.

parents_are_directories([Part, ".." | Parts], Mod, Before) ->
    Path =
        case Before of
            [] -> Part;
            _ -> filename:join(lists:reverse(Before, [Part]))
        end,
    case read_link_info(Path, Mod) of
        {ok, #file_info{type = directory}} -> parents_are_directories(Parts, Mod, Before);
        _ -> false
    end;
parents_are_directories([Part | Parts], Mod, Before) ->
    parents_are_directories(Parts, Mod, [Part | Before]);
parents_are_directories([], _, _) ->
    true.

% --- Queries ---

is_dir(Name) -> is_dir(Name, file).

is_dir(Name, Mod) when is_atom(Mod) -> type(Name, Mod) =:= directory.

is_file(Name) -> is_file(Name, file).

is_file(Name, Mod) when is_atom(Mod) -> lists:member(type(Name, Mod), [regular, directory]).

is_regular(Name) -> is_regular(Name, file).

is_regular(Name, Mod) when is_atom(Mod) -> type(Name, Mod) =:= regular.

last_modified(Name) -> last_modified(Name, file).

% The modification date and time, or 0 when there is no such file.
last_modified(Name, Mod) when is_atom(Mod) ->
    case read_file_info(Name, Mod) of
        {ok, #file_info{mtime = MTime}} -> MTime;
        _ -> 0
    end.

file_size(Name) -> file_size(Name, file).

% The size in bytes, or 0 when there is no such file.
file_size(Name, Mod) when is_atom(Mod) ->
    case read_file_info(Name, Mod) of
        {ok, #file_info{size = Size}} -> Size;
        _ -> 0
    end.

type(Name, Mod) ->
    case read_file_info(Name, Mod) of
        {ok, #file_info{type = Type}} -> Type;
        _ -> none
    end.

% The answers of Mod; erl_prim_loader's bare error becomes {error, erl_prim_loader}.
read_file_info(Name, file) -> file:read_file_info(Name);
read_file_info(Name, erl_prim_loader) -> loader_answer(erl_prim_loader:read_file_info(Name));
read_file_info(Name, Mod) -> Mod:read_file_info(Name).

read_link_info(Name, file) -> file:read_link_info(Name);
read_link_info(Name, erl_prim_loader) -> loader_answer(erl_prim_loader:read_link_info(Name));
read_link_info(Name, Mod) -> Mod:read_link_info(Name).

list_dir(Dir, file) -> file:list_dir(Dir);
list_dir(Dir, erl_prim_loader) -> loader_answer(erl_prim_loader:list_dir(Dir));
list_dir(Dir, Mod) -> Mod:list_dir(Dir).

loader_answer(error) -> {error, erl_prim_loader};
loader_answer(Answer) -> Answer.

% --- Folding over files ---

fold_files(Dir, RegExp, Recursive, Fun, Acc) -> fold_files(Dir, RegExp, Recursive, Fun, Acc, file).

% Fun(Name, Acc) over the regular files in Dir (and below when Recursive) whose base name matches RegExp. A name
% that is a binary (not Unicode) is matched by the uncompiled expression.
fold_files(Dir, RegExp, Recursive, Fun, Acc, Mod) when is_atom(Mod) ->
    {ok, Compiled} = re:compile(RegExp, [unicode]),
    fold_dir(Dir, {Compiled, RegExp, Recursive, Fun, Mod}, Acc).

fold_dir(Dir, Fold, Acc) ->
    case list_dir(Dir, element(5, Fold)) of
        {ok, Names} -> lists:foldl(fun(Name, In) -> fold_entry(Dir, Name, Fold, In) end, Acc, Names);
        {error, _} -> Acc
    end.

fold_entry(Dir, Name, {Compiled, RegExp, Recursive, Fun, Mod} = Fold, Acc) ->
    Full = filename:join(Dir, Name),
    case is_regular(Full, Mod) of
        true ->
            try re:run(Name, expression(Name, Compiled, RegExp), [{capture, none}]) of
                match -> Fun(Full, Acc);
                nomatch -> Acc
            catch
                _:_ -> Acc
            end;
        false ->
            case Recursive andalso is_dir(Full, Mod) of
                true -> fold_dir(Full, Fold, Acc);
                false -> Acc
            end
    end.

expression(Name, _, RegExp) when is_binary(Name) -> RegExp;
expression(_, Compiled, _) -> Compiled.

% --- Creating directories ---

% Make the parent directories of Name.
ensure_dir("/") -> ok;
ensure_dir(Name) -> ensure_path(filename:dirname(Name)).

% Make every directory of Path, Path included; a directory made meanwhile by another process counts as made.
ensure_path("/") ->
    ok;
ensure_path(Path) ->
    case is_dir(Path, file) of
        true ->
            ok;
        false ->
            case filename:dirname(Path) of
                Path ->
                    {error, einval};
                Parent ->
                    _ = ensure_path(Parent),
                    made(file:make_dir(Path), Path)
            end
    end.

made({error, eexist} = Error, Path) ->
    case is_dir(Path, file) of
        true -> ok;
        false -> Error
    end;
made(Result, _) ->
    Result.

% --- Finding files by rules ---

find_file(Name, Dir) -> find_file(Name, Dir, []).

% The first regular file Name in a directory a rule {ObjSuffix, SrcSuffix} derives from Dir (wildcards allowed);
% no rules means kernel's source_search_rules or the defaults.
find_file(Name, Dir, []) -> find_file(Name, Dir, search_rules());
find_file(Name, Dir, Rules) -> try_directories([R || {_, _} = R <- Rules], Name, Dir).

find_source(Path) -> find_source(filename:basename(Path), filename:dirname(Path)).

find_source(Name, Dir) -> find_source(Name, Dir, []).

% The source of an object file by rules {ObjExtension, SrcExtension, DirRules}; Dir itself is tried first.
find_source(Name, Dir, []) -> find_source(Name, Dir, search_rules());
find_source(Name, Dir, Rules) -> try_extensions([R || {_, _, _} = R <- Rules], Name, Dir).

try_extensions(Rules, Name, Dir) ->
    Extension = filename:extension(Name),
    try_extensions(Rules, filename:rootname(Name, Extension), Dir, Extension).

try_extensions([{Extension, Source, Rules} | Rest], Root, Dir, Extension) when is_list(Source), is_list(Rules) ->
    Local = {"", ""},
    case try_directories([Local | [R || R <- Rules, R =/= Local]], Root ++ Source, Dir) of
        {ok, Found} -> {ok, Found};
        _ -> try_extensions(Rest, Root, Dir, Extension)
    end;
try_extensions([_ | Rest], Root, Dir, Extension) ->
    try_extensions(Rest, Root, Dir, Extension);
try_extensions([], _, _, _) ->
    {error, not_found}.

try_directories([{From, To} | Rest], Name, Dir) when is_list(From), is_list(To) ->
    case try_directory(Dir, Name, From, To) of
        {ok, Found} -> {ok, Found};
        error -> try_directories(Rest, Name, Dir)
    end;
try_directories([], _, _) ->
    {error, not_found}.

% Dir ending in From, with To in its place: Name there, or the first regular file its wildcard finds.
try_directory(Dir, Name, From, To) ->
    case lists:suffix(From, Dir) of
        true ->
            Source = filename:join(lists:sublist(Dir, 1, length(Dir) - length(From)) ++ To, Name),
            case is_regular(Source) of
                true -> {ok, Source};
                false -> first_regular(wildcard(Source))
            end;
        false ->
            error
    end.

first_regular([Name | Names]) ->
    case is_regular(Name) of
        true -> {ok, Name};
        false -> first_regular(Names)
    end;
first_regular([]) ->
    error.

search_rules() ->
    case application:get_env(kernel, source_search_rules) of
        {ok, Rules} when is_list(Rules), Rules =/= [] -> Rules;
        _ -> default_search_rules()
    end.

default_search_rules() ->
    Erl = erl_source_rules(),
    C = [{"priv", "c_src"}, {"priv", "src"}, {"bin", "c_src"}, {"bin", "src"}, {"", "src"}],
    [
        {".beam", ".erl", Erl},
        {".erl", ".yrl", []},
        {"", ".src", Erl},
        {".so", ".c", C},
        {".o", ".c", C},
        {"", ".c", C},
        {"", ".in", Erl ++ C},
        {".beam", ".asn1", [{"ebin", "src"}, {"ebin", "asn1"}]},
        {"", ""}
        | Erl
    ].

erl_source_rules() ->
    [{"ebin", "src"}, {"ebin", "esrc"}, {"ebin", filename:join("src", "*")}, {"ebin", filename:join("esrc", "*")}].

% --- Safe relative paths ---

% Path without "." and ".." parts, or unsafe when it is absolute or climbs above its start, also through the
% symbolic links below Cwd it passes (a link seen twice is unsafe).
safe_relative_path(Path, "") -> safe_relative_path(Path, ".");
safe_relative_path(Path, Cwd) -> safe_parts(filename:split(Path), Cwd, sets:new(), []).

safe_parts([], _, _, []) ->
    "";
safe_parts([], _, _, Kept) ->
    filename:join(Kept);
safe_parts([Dot | Parts], Cwd, Seen, Kept) when Dot =:= "."; Dot =:= <<".">> ->
    safe_parts(Parts, Cwd, Seen, Kept);
safe_parts([Up | _], _, _, []) when Up =:= ".."; Up =:= <<"..">> ->
    unsafe;
safe_parts([Up | Parts], Cwd, Seen, Kept) when Up =:= ".."; Up =:= <<"..">> ->
    safe_parts(Parts, Cwd, Seen, lists:droplast(Kept));
safe_parts([clear | Parts], Cwd, _, Kept) ->
    safe_parts(Parts, Cwd, sets:new(), Kept);
safe_parts([Part | Parts], Cwd, Seen, Kept) ->
    case filename:pathtype(Part) of
        relative -> safe_part(Part, Parts, Cwd, Seen, Kept);
        _ -> unsafe
    end.

% A part that may be a symbolic link: its target's parts replace it, the links seen so far cleared after them.
safe_part(Part, Parts, Cwd, Seen, Kept) ->
    Dir = filename:join([Cwd | Kept]),
    case file:read_link(filename:join(Dir, Part)) of
        {ok, Target} ->
            Link = filename:join(Dir, Target),
            case sets:is_element(Link, Seen) of
                true -> unsafe;
                false -> safe_parts(filename:split(Target) ++ [clear | Parts], Cwd, sets:add_element(Link, Seen), Kept)
            end;
        {error, _} ->
            safe_parts(Parts, Cwd, Seen, Kept ++ [Part])
    end.
