%% Clause's replacement for OTP's erl_prim_loader (docs/library.md#runtime-module-replacements), generated from
%% erl_prim_loader.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(erl_prim_loader).
-export([
    get_cwd/0,
    get_cwd/1,
    get_file/1,
    get_modules/2,
    get_modules/3,
    get_path/0,
    is_basename/1,
    list_dir/1,
    prim_get_cwd/2,
    prim_init/0,
    prim_list_dir/2,
    prim_read_file/2,
    prim_read_file_info/3,
    purge_archive_cache/0,
    read_file/1,
    read_file_info/1,
    read_link_info/1,
    set_path/1,
    set_primary_archive/4,
    start/0
]).

get_cwd() -> erlang:error({not_implemented, get_cwd, 0}).
get_cwd(_) -> erlang:error({not_implemented, get_cwd, 1}).
get_file(_) -> erlang:error({not_implemented, get_file, 1}).
get_modules(_, _) -> erlang:error({not_implemented, get_modules, 2}).
get_modules(_, _, _) -> erlang:error({not_implemented, get_modules, 3}).
get_path() -> erlang:error({not_implemented, get_path, 0}).
is_basename(_) -> erlang:error({not_implemented, is_basename, 1}).
list_dir(_) -> erlang:error({not_implemented, list_dir, 1}).
prim_get_cwd(_, _) -> erlang:error({not_implemented, prim_get_cwd, 2}).
prim_init() -> erlang:error({not_implemented, prim_init, 0}).
prim_list_dir(_, _) -> erlang:error({not_implemented, prim_list_dir, 2}).
prim_read_file(_, _) -> erlang:error({not_implemented, prim_read_file, 2}).
prim_read_file_info(_, _, _) -> erlang:error({not_implemented, prim_read_file_info, 3}).
purge_archive_cache() -> erlang:error({not_implemented, purge_archive_cache, 0}).
read_file(_) -> erlang:error({not_implemented, read_file, 1}).
read_file_info(_) -> erlang:error({not_implemented, read_file_info, 1}).
read_link_info(_) -> erlang:error({not_implemented, read_link_info, 1}).
set_path(_) -> erlang:error({not_implemented, set_path, 1}).
set_primary_archive(_, _, _, _) -> erlang:error({not_implemented, set_primary_archive, 4}).
start() -> erlang:error({not_implemented, start, 0}).
