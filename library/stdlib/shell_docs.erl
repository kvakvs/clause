%% Clause's replacement for OTP's shell_docs (docs/library.md#runtime-module-replacements), generated from
%% shell_docs.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}); a catalog builtin replaces its clause_builtin_placeholder body.
-module(shell_docs).
-export([
    extract_type_specs/1,
    get_callback_doc/3,
    get_doc/1,
    get_doc/3,
    get_type_doc/3,
    normalize/1,
    render/2,
    render/3,
    render/4,
    render/5,
    render_callback/2,
    render_callback/3,
    render_callback/4,
    render_callback/5,
    render_type/2,
    render_type/3,
    render_type/4,
    render_type/5,
    supported_tags/0,
    validate/1
]).
-include("shell_docs.types.hrl").

extract_type_specs(_) -> erlang:error({not_implemented, extract_type_specs, 1}).
get_callback_doc(_, _, _) -> erlang:error({not_implemented, get_callback_doc, 3}).
get_doc(_) -> erlang:error({not_implemented, get_doc, 1}).
get_doc(_, _, _) -> erlang:error({not_implemented, get_doc, 3}).
get_type_doc(_, _, _) -> erlang:error({not_implemented, get_type_doc, 3}).
normalize(_) -> erlang:error({not_implemented, normalize, 1}).
render(_, _) -> erlang:error({not_implemented, render, 2}).
render(_, _, _) -> erlang:error({not_implemented, render, 3}).
render(_, _, _, _) -> erlang:error({not_implemented, render, 4}).
render(_, _, _, _, _) -> erlang:error({not_implemented, render, 5}).
render_callback(_, _) -> erlang:error({not_implemented, render_callback, 2}).
render_callback(_, _, _) -> erlang:error({not_implemented, render_callback, 3}).
render_callback(_, _, _, _) -> erlang:error({not_implemented, render_callback, 4}).
render_callback(_, _, _, _, _) -> erlang:error({not_implemented, render_callback, 5}).
render_type(_, _) -> erlang:error({not_implemented, render_type, 2}).
render_type(_, _, _) -> erlang:error({not_implemented, render_type, 3}).
render_type(_, _, _, _) -> erlang:error({not_implemented, render_type, 4}).
render_type(_, _, _, _, _) -> erlang:error({not_implemented, render_type, 5}).
supported_tags() -> erlang:error({not_implemented, supported_tags, 0}).
validate(_) -> erlang:error({not_implemented, validate, 1}).
