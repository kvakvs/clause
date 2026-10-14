%% Clause's replacement for OTP's math (docs/library.md#runtime-module-replacements), generated from
%% math.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Every function is a runtime builtin that replaces its stub (plan step 41V,
%% docs/builtins.md#math-module).
-module(math).
-export([
    acos/1,
    acosh/1,
    asin/1,
    asinh/1,
    atan/1,
    atan2/2,
    atanh/1,
    ceil/1,
    cos/1,
    cosh/1,
    erf/1,
    erfc/1,
    exp/1,
    floor/1,
    fmod/2,
    log/1,
    log10/1,
    log2/1,
    pi/0,
    pow/2,
    sin/1,
    sinh/1,
    sqrt/1,
    tan/1,
    tanh/1,
    tau/0
]).

acos(_) -> erlang:error({not_implemented, acos, 1}).
acosh(_) -> erlang:error({not_implemented, acosh, 1}).
asin(_) -> erlang:error({not_implemented, asin, 1}).
asinh(_) -> erlang:error({not_implemented, asinh, 1}).
atan(_) -> erlang:error({not_implemented, atan, 1}).
atan2(_, _) -> erlang:error({not_implemented, atan2, 2}).
atanh(_) -> erlang:error({not_implemented, atanh, 1}).
ceil(_) -> erlang:error({not_implemented, ceil, 1}).
cos(_) -> erlang:error({not_implemented, cos, 1}).
cosh(_) -> erlang:error({not_implemented, cosh, 1}).
erf(_) -> erlang:error({not_implemented, erf, 1}).
erfc(_) -> erlang:error({not_implemented, erfc, 1}).
exp(_) -> erlang:error({not_implemented, exp, 1}).
floor(_) -> erlang:error({not_implemented, floor, 1}).
fmod(_, _) -> erlang:error({not_implemented, fmod, 2}).
log(_) -> erlang:error({not_implemented, log, 1}).
log10(_) -> erlang:error({not_implemented, log10, 1}).
log2(_) -> erlang:error({not_implemented, log2, 1}).
pi() -> erlang:error({not_implemented, pi, 0}).
pow(_, _) -> erlang:error({not_implemented, pow, 2}).
sin(_) -> erlang:error({not_implemented, sin, 1}).
sinh(_) -> erlang:error({not_implemented, sinh, 1}).
sqrt(_) -> erlang:error({not_implemented, sqrt, 1}).
tan(_) -> erlang:error({not_implemented, tan, 1}).
tanh(_) -> erlang:error({not_implemented, tanh, 1}).
tau() -> erlang:error({not_implemented, tau, 0}).
