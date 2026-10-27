%% Clause's erl_debugger (plan 21, docs/libraries/library.md#runtime-module-replacements): Clause has no debugger
%% interface, so it answers as an OTP emulator started without +D: supported/0 is false and every other function
%% raises undef, as the emulator's missing debugger functions do.
-module(erl_debugger).
-export([
    breakpoint/3,
    breakpoints/1,
    breakpoints/3,
    instrumentations/0,
    peek_stack_frame_slot/4,
    peek_xreg/3,
    register/1,
    stack_frames/2,
    supported/0,
    toggle_instrumentations/1,
    unregister/2,
    whereis/0,
    xregs_count/1
]).
-include("erl_debugger.types.hrl").

supported() -> false.

breakpoint(_, _, _) -> unsupported().
breakpoints(_) -> unsupported().
breakpoints(_, _, _) -> unsupported().
instrumentations() -> unsupported().
peek_stack_frame_slot(_, _, _, _) -> unsupported().
peek_xreg(_, _, _) -> unsupported().
register(_) -> unsupported().
stack_frames(_, _) -> unsupported().
toggle_instrumentations(_) -> unsupported().
unregister(_, _) -> unsupported().
whereis() -> unsupported().
xregs_count(_) -> unsupported().

unsupported() -> erlang:error(undef).
