%% Clause's erl_anno (plan 21 step LIB8, docs/builtins.md#compiler-tables): every function is a runtime builtin
%% working on OTP's annotation shapes (a location, or a list of {Item, Value} pairs), errors included.
-module(erl_anno).
-export([
    column/1,
    end_location/1,
    file/1,
    from_term/1,
    generated/1,
    is_anno/1,
    line/1,
    location/1,
    new/1,
    record/1,
    set_end_location/2,
    set_file/2,
    set_generated/2,
    set_line/2,
    set_location/2,
    set_record/2,
    set_text/2,
    text/1,
    to_term/1
]).
-include("erl_anno.types.hrl").

column(_) -> clause_builtin_placeholder.
end_location(_) -> clause_builtin_placeholder.
file(_) -> clause_builtin_placeholder.
from_term(_) -> clause_builtin_placeholder.
generated(_) -> clause_builtin_placeholder.
is_anno(_) -> clause_builtin_placeholder.
line(_) -> clause_builtin_placeholder.
location(_) -> clause_builtin_placeholder.
new(_) -> clause_builtin_placeholder.
record(_) -> clause_builtin_placeholder.
set_end_location(_, _) -> clause_builtin_placeholder.
set_file(_, _) -> clause_builtin_placeholder.
set_generated(_, _) -> clause_builtin_placeholder.
set_line(_, _) -> clause_builtin_placeholder.
set_location(_, _) -> clause_builtin_placeholder.
set_record(_, _) -> clause_builtin_placeholder.
set_text(_, _) -> clause_builtin_placeholder.
text(_) -> clause_builtin_placeholder.
to_term(_) -> clause_builtin_placeholder.
