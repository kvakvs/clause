%% Clause's replacement for OTP's code (docs/library.md#runtime-module-replacements), generated from
%% code.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). The queries about loaded modules are runtime builtins answering for the
%% linked program without a code server (plan step 41Z, docs/builtins.md#code-module); code paths and loading
%% wait for step 71.
-module(code).
-export([
    add_path/1,
    add_path/2,
    add_patha/1,
    add_patha/2,
    add_paths/1,
    add_paths/2,
    add_pathsa/1,
    add_pathsa/2,
    add_pathsz/1,
    add_pathsz/2,
    add_pathz/1,
    add_pathz/2,
    all_available/0,
    all_loaded/0,
    atomic_load/1,
    clash/0,
    clear_cache/0,
    compiler_dir/0,
    coverage_support/0,
    del_path/1,
    del_paths/1,
    delete/1,
    ensure_loaded/1,
    ensure_modules_loaded/1,
    finish_loading/1,
    get_chunk/2,
    get_coverage/2,
    get_coverage_mode/0,
    get_coverage_mode/1,
    get_debug_info/1,
    get_doc/1,
    get_doc/2,
    get_mode/0,
    get_object_code/1,
    get_path/0,
    is_loaded/1,
    is_sticky/1,
    lib_dir/0,
    lib_dir/1,
    lib_dir/2,
    load_abs/1,
    load_abs/2,
    load_binary/3,
    load_file/1,
    modified_modules/0,
    module_md5/1,
    module_status/0,
    module_status/1,
    objfile_extension/0,
    prepare_loading/1,
    priv_dir/1,
    purge/1,
    replace_path/2,
    replace_path/3,
    reset_coverage/1,
    root_dir/0,
    set_coverage_mode/1,
    set_path/1,
    set_path/2,
    soft_purge/1,
    start_link/0,
    stick_dir/1,
    stick_mod/1,
    stop/0,
    unstick_dir/1,
    unstick_mod/1,
    where_is_file/1,
    where_is_file/2,
    which/1
]).
-include("code.types.hrl").

add_path(_) -> erlang:error({not_implemented, add_path, 1}).
add_path(_, _) -> erlang:error({not_implemented, add_path, 2}).
add_patha(_) -> erlang:error({not_implemented, add_patha, 1}).
add_patha(_, _) -> erlang:error({not_implemented, add_patha, 2}).
add_paths(_) -> erlang:error({not_implemented, add_paths, 1}).
add_paths(_, _) -> erlang:error({not_implemented, add_paths, 2}).
add_pathsa(_) -> erlang:error({not_implemented, add_pathsa, 1}).
add_pathsa(_, _) -> erlang:error({not_implemented, add_pathsa, 2}).
add_pathsz(_) -> erlang:error({not_implemented, add_pathsz, 1}).
add_pathsz(_, _) -> erlang:error({not_implemented, add_pathsz, 2}).
add_pathz(_) -> erlang:error({not_implemented, add_pathz, 1}).
add_pathz(_, _) -> erlang:error({not_implemented, add_pathz, 2}).
all_available() -> erlang:error({not_implemented, all_available, 0}).
all_loaded() -> clause_builtin_placeholder.
atomic_load(_) -> erlang:error({not_implemented, atomic_load, 1}).
clash() -> erlang:error({not_implemented, clash, 0}).
clear_cache() -> erlang:error({not_implemented, clear_cache, 0}).
compiler_dir() -> erlang:error({not_implemented, compiler_dir, 0}).
coverage_support() -> erlang:error({not_implemented, coverage_support, 0}).
del_path(_) -> erlang:error({not_implemented, del_path, 1}).
del_paths(_) -> erlang:error({not_implemented, del_paths, 1}).
delete(_) -> erlang:error({not_implemented, delete, 1}).
ensure_loaded(_) -> clause_builtin_placeholder.
ensure_modules_loaded(_) -> clause_builtin_placeholder.
finish_loading(_) -> erlang:error({not_implemented, finish_loading, 1}).
get_chunk(_, _) -> erlang:error({not_implemented, get_chunk, 2}).
get_coverage(_, _) -> erlang:error({not_implemented, get_coverage, 2}).
get_coverage_mode() -> erlang:error({not_implemented, get_coverage_mode, 0}).
get_coverage_mode(_) -> erlang:error({not_implemented, get_coverage_mode, 1}).
get_debug_info(_) -> erlang:error({not_implemented, get_debug_info, 1}).
get_doc(_) -> erlang:error({not_implemented, get_doc, 1}).
get_doc(_, _) -> erlang:error({not_implemented, get_doc, 2}).
get_mode() -> erlang:error({not_implemented, get_mode, 0}).
get_object_code(_) -> erlang:error({not_implemented, get_object_code, 1}).
get_path() -> erlang:error({not_implemented, get_path, 0}).
is_loaded(_) -> clause_builtin_placeholder.
is_sticky(_) -> clause_builtin_placeholder.
lib_dir() -> erlang:error({not_implemented, lib_dir, 0}).
lib_dir(_) -> erlang:error({not_implemented, lib_dir, 1}).
lib_dir(_, _) -> erlang:error({not_implemented, lib_dir, 2}).
load_abs(_) -> erlang:error({not_implemented, load_abs, 1}).
load_abs(_, _) -> erlang:error({not_implemented, load_abs, 2}).
load_binary(_, _, _) -> erlang:error({not_implemented, load_binary, 3}).
load_file(_) -> erlang:error({not_implemented, load_file, 1}).
modified_modules() -> erlang:error({not_implemented, modified_modules, 0}).
module_md5(_) -> erlang:error({not_implemented, module_md5, 1}).
module_status() -> clause_builtin_placeholder.
module_status(_) -> clause_builtin_placeholder.
objfile_extension() -> erlang:error({not_implemented, objfile_extension, 0}).
prepare_loading(_) -> erlang:error({not_implemented, prepare_loading, 1}).
priv_dir(_) -> erlang:error({not_implemented, priv_dir, 1}).
purge(_) -> erlang:error({not_implemented, purge, 1}).
replace_path(_, _) -> erlang:error({not_implemented, replace_path, 2}).
replace_path(_, _, _) -> erlang:error({not_implemented, replace_path, 3}).
reset_coverage(_) -> erlang:error({not_implemented, reset_coverage, 1}).
root_dir() -> erlang:error({not_implemented, root_dir, 0}).
set_coverage_mode(_) -> erlang:error({not_implemented, set_coverage_mode, 1}).
set_path(_) -> erlang:error({not_implemented, set_path, 1}).
set_path(_, _) -> erlang:error({not_implemented, set_path, 2}).
soft_purge(_) -> erlang:error({not_implemented, soft_purge, 1}).
start_link() -> erlang:error({not_implemented, start_link, 0}).
stick_dir(_) -> erlang:error({not_implemented, stick_dir, 1}).
stick_mod(_) -> erlang:error({not_implemented, stick_mod, 1}).
stop() -> erlang:error({not_implemented, stop, 0}).
unstick_dir(_) -> erlang:error({not_implemented, unstick_dir, 1}).
unstick_mod(_) -> erlang:error({not_implemented, unstick_mod, 1}).
where_is_file(_) -> erlang:error({not_implemented, where_is_file, 1}).
where_is_file(_, _) -> erlang:error({not_implemented, where_is_file, 2}).
which(_) -> clause_builtin_placeholder.
