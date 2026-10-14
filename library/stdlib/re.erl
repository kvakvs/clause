%% Clause's replacement for OTP's re (docs/library.md#runtime-module-replacements), generated from
%% re.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(re).
-export([
    compile/1,
    compile/2,
    grun/3,
    import/1,
    inspect/2,
    internal_run/4,
    replace/3,
    replace/4,
    run/2,
    run/3,
    split/2,
    split/3,
    ucompile/2,
    urun/3,
    version/0
]).

-include("re.types.hrl").

compile(_) -> erlang:error({not_implemented, compile, 1}).
compile(_, _) -> erlang:error({not_implemented, compile, 2}).
grun(_, _, _) -> erlang:error({not_implemented, grun, 3}).
import(_) -> erlang:error({not_implemented, import, 1}).
inspect(_, _) -> erlang:error({not_implemented, inspect, 2}).
internal_run(_, _, _, _) -> erlang:error({not_implemented, internal_run, 4}).
replace(_, _, _) -> erlang:error({not_implemented, replace, 3}).
replace(_, _, _, _) -> erlang:error({not_implemented, replace, 4}).
run(_, _) -> erlang:error({not_implemented, run, 2}).
run(_, _, _) -> erlang:error({not_implemented, run, 3}).
split(_, _) -> erlang:error({not_implemented, split, 2}).
split(_, _, _) -> erlang:error({not_implemented, split, 3}).
ucompile(_, _) -> erlang:error({not_implemented, ucompile, 2}).
urun(_, _, _) -> erlang:error({not_implemented, urun, 3}).
version() -> erlang:error({not_implemented, version, 0}).
