%% OTP's dbg, declined (plan 21 step MIM5b, docs/libraries/runtime_tools.md#dbg): Clause has no tracing, so every
%% function is a stub raising error({not_implemented, Name, Arity}). Code calling dbg (hackney's and gun's debug
%% helpers) compiles and fails only when called. Generated from dbg.terms by tests/compiler/library/library_modules.py.
-module(dbg).
-export([
    c/3,
    c/4,
    cn/1,
    ctp/0,
    ctp/1,
    ctp/2,
    ctp/3,
    ctpe/1,
    ctpg/0,
    ctpg/1,
    ctpg/2,
    ctpg/3,
    ctpl/0,
    ctpl/1,
    ctpl/2,
    ctpl/3,
    deliver_and_flush/1,
    deliver_and_flush/2,
    dhandler/2,
    do_relay/3,
    dtp/0,
    dtp/1,
    erlang_trace/4,
    flush_trace_port/0,
    flush_trace_port/1,
    fun2ms/1,
    get_info/0,
    get_tracer/0,
    get_tracer/1,
    h/0,
    h/1,
    i/0,
    ln/0,
    ltp/0,
    match_0_9/1,
    match_front/2,
    match_rear/2,
    n/1,
    p/1,
    p/2,
    rtp/1,
    session/2,
    session_create/1,
    session_destroy/1,
    start/0,
    stop/0,
    stop_clear/0,
    stop_trace_client/1,
    tp/2,
    tp/3,
    tp/4,
    tpe/2,
    tpl/2,
    tpl/3,
    tpl/4,
    trace_client/2,
    trace_client/3,
    trace_port/2,
    trace_port_control/1,
    trace_port_control/2,
    tracer/0,
    tracer/2,
    tracer/3,
    transform_flags/1,
    wrap_postsort/1,
    wrap_presort/2,
    wrap_sort/2,
    wrap_sortfix/2,
    wtp/1
]).
-include("dbg.types.hrl").

c(_, _, _) -> erlang:error({not_implemented, c, 3}).
c(_, _, _, _) -> erlang:error({not_implemented, c, 4}).
cn(_) -> erlang:error({not_implemented, cn, 1}).
ctp() -> erlang:error({not_implemented, ctp, 0}).
ctp(_) -> erlang:error({not_implemented, ctp, 1}).
ctp(_, _) -> erlang:error({not_implemented, ctp, 2}).
ctp(_, _, _) -> erlang:error({not_implemented, ctp, 3}).
ctpe(_) -> erlang:error({not_implemented, ctpe, 1}).
ctpg() -> erlang:error({not_implemented, ctpg, 0}).
ctpg(_) -> erlang:error({not_implemented, ctpg, 1}).
ctpg(_, _) -> erlang:error({not_implemented, ctpg, 2}).
ctpg(_, _, _) -> erlang:error({not_implemented, ctpg, 3}).
ctpl() -> erlang:error({not_implemented, ctpl, 0}).
ctpl(_) -> erlang:error({not_implemented, ctpl, 1}).
ctpl(_, _) -> erlang:error({not_implemented, ctpl, 2}).
ctpl(_, _, _) -> erlang:error({not_implemented, ctpl, 3}).
deliver_and_flush(_) -> erlang:error({not_implemented, deliver_and_flush, 1}).
deliver_and_flush(_, _) -> erlang:error({not_implemented, deliver_and_flush, 2}).
dhandler(_, _) -> erlang:error({not_implemented, dhandler, 2}).
do_relay(_, _, _) -> erlang:error({not_implemented, do_relay, 3}).
dtp() -> erlang:error({not_implemented, dtp, 0}).
dtp(_) -> erlang:error({not_implemented, dtp, 1}).
erlang_trace(_, _, _, _) -> erlang:error({not_implemented, erlang_trace, 4}).
flush_trace_port() -> erlang:error({not_implemented, flush_trace_port, 0}).
flush_trace_port(_) -> erlang:error({not_implemented, flush_trace_port, 1}).
fun2ms(_) -> erlang:error({not_implemented, fun2ms, 1}).
get_info() -> erlang:error({not_implemented, get_info, 0}).
get_tracer() -> erlang:error({not_implemented, get_tracer, 0}).
get_tracer(_) -> erlang:error({not_implemented, get_tracer, 1}).
h() -> erlang:error({not_implemented, h, 0}).
h(_) -> erlang:error({not_implemented, h, 1}).
i() -> erlang:error({not_implemented, i, 0}).
ln() -> erlang:error({not_implemented, ln, 0}).
ltp() -> erlang:error({not_implemented, ltp, 0}).
match_0_9(_) -> erlang:error({not_implemented, match_0_9, 1}).
match_front(_, _) -> erlang:error({not_implemented, match_front, 2}).
match_rear(_, _) -> erlang:error({not_implemented, match_rear, 2}).
n(_) -> erlang:error({not_implemented, n, 1}).
p(_) -> erlang:error({not_implemented, p, 1}).
p(_, _) -> erlang:error({not_implemented, p, 2}).
rtp(_) -> erlang:error({not_implemented, rtp, 1}).
session(_, _) -> erlang:error({not_implemented, session, 2}).
session_create(_) -> erlang:error({not_implemented, session_create, 1}).
session_destroy(_) -> erlang:error({not_implemented, session_destroy, 1}).
start() -> erlang:error({not_implemented, start, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
stop_clear() -> erlang:error({not_implemented, stop_clear, 0}).
stop_trace_client(_) -> erlang:error({not_implemented, stop_trace_client, 1}).
tp(_, _) -> erlang:error({not_implemented, tp, 2}).
tp(_, _, _) -> erlang:error({not_implemented, tp, 3}).
tp(_, _, _, _) -> erlang:error({not_implemented, tp, 4}).
tpe(_, _) -> erlang:error({not_implemented, tpe, 2}).
tpl(_, _) -> erlang:error({not_implemented, tpl, 2}).
tpl(_, _, _) -> erlang:error({not_implemented, tpl, 3}).
tpl(_, _, _, _) -> erlang:error({not_implemented, tpl, 4}).
trace_client(_, _) -> erlang:error({not_implemented, trace_client, 2}).
trace_client(_, _, _) -> erlang:error({not_implemented, trace_client, 3}).
trace_port(_, _) -> erlang:error({not_implemented, trace_port, 2}).
trace_port_control(_) -> erlang:error({not_implemented, trace_port_control, 1}).
trace_port_control(_, _) -> erlang:error({not_implemented, trace_port_control, 2}).
tracer() -> erlang:error({not_implemented, tracer, 0}).
tracer(_, _) -> erlang:error({not_implemented, tracer, 2}).
tracer(_, _, _) -> erlang:error({not_implemented, tracer, 3}).
transform_flags(_) -> erlang:error({not_implemented, transform_flags, 1}).
wrap_postsort(_) -> erlang:error({not_implemented, wrap_postsort, 1}).
wrap_presort(_, _) -> erlang:error({not_implemented, wrap_presort, 2}).
wrap_sort(_, _) -> erlang:error({not_implemented, wrap_sort, 2}).
wrap_sortfix(_, _) -> erlang:error({not_implemented, wrap_sortfix, 2}).
wtp(_) -> erlang:error({not_implemented, wtp, 1}).
