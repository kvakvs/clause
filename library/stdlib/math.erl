%% Clause's replacement for OTP's math (docs/library.md#runtime-module-replacements), generated from
%% math.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Every function is a runtime builtin that replaces its placeholder (plan step 41V,
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

acos(_) -> clause_builtin_placeholder.
acosh(_) -> clause_builtin_placeholder.
asin(_) -> clause_builtin_placeholder.
asinh(_) -> clause_builtin_placeholder.
atan(_) -> clause_builtin_placeholder.
atan2(_, _) -> clause_builtin_placeholder.
atanh(_) -> clause_builtin_placeholder.
ceil(_) -> clause_builtin_placeholder.
cos(_) -> clause_builtin_placeholder.
cosh(_) -> clause_builtin_placeholder.
erf(_) -> clause_builtin_placeholder.
erfc(_) -> clause_builtin_placeholder.
exp(_) -> clause_builtin_placeholder.
floor(_) -> clause_builtin_placeholder.
fmod(_, _) -> clause_builtin_placeholder.
log(_) -> clause_builtin_placeholder.
log10(_) -> clause_builtin_placeholder.
log2(_) -> clause_builtin_placeholder.
pi() -> clause_builtin_placeholder.
pow(_, _) -> clause_builtin_placeholder.
sin(_) -> clause_builtin_placeholder.
sinh(_) -> clause_builtin_placeholder.
sqrt(_) -> clause_builtin_placeholder.
tan(_) -> clause_builtin_placeholder.
tanh(_) -> clause_builtin_placeholder.
tau() -> clause_builtin_placeholder.
