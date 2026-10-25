-module(shell_default).
-export([
    help/0,
    lc/1,
    c/1,
    c/2,
    c/3,
    nc/1,
    nl/1,
    l/1,
    i/0,
    pid/3,
    i/3,
    pi/1,
    pi/3,
    m/0,
    m/1,
    lm/0,
    mm/0,
    memory/0,
    memory/1,
    uptime/0,
    erlangrc/1,
    bi/1,
    regs/0,
    flush/0,
    pwd/0,
    ls/0,
    ls/1,
    cd/1,
    y/1,
    y/2,
    xm/1,
    bt/1,
    q/0,
    h/1,
    h/2,
    h/3,
    ht/1,
    ht/2,
    ht/3,
    hcb/1,
    hcb/2,
    hcb/3,
    ni/0,
    nregs/0
]).
-export([
    ih/0,
    iv/0,
    im/0,
    ii/1,
    ii/2,
    iq/1,
    ini/1,
    ini/2,
    inq/1,
    ib/2,
    ib/3,
    ir/2,
    ir/3,
    ibd/2,
    ibe/2,
    iba/3,
    ibc/3,
    ic/0,
    ir/1,
    ir/0,
    il/0,
    ipb/0,
    ipb/1,
    iaa/1,
    iaa/2,
    ist/1,
    ia/1,
    ia/2,
    ia/3,
    ia/4,
    ip/0
]).
-export(['$handle_undefined_function'/2]).

% The shell's default local functions (plan 21 step LIB74, docs/library.md#interactive-shell): the commands of c
% and of the debugger's i module, callable without a module prefix. A user_default module's functions come first.

%% Print the help of every shell command.
help() ->
    shell:help(),
    c:help(),
    io:format(<<"** commands in module i (interpreter interface) **\n">>),
    io:format(<<"ih()       -- print help for the i module\n">>),
    true.

bi(I) -> c:bi(I).
bt(Pid) -> c:bt(Pid).
c(File) -> c:c(File).
c(File, Opt) -> c:c(File, Opt).
c(File, Opt, Filter) -> c:c(File, Opt, Filter).
cd(D) -> c:cd(D).
erlangrc(X) -> c:erlangrc(X).
flush() -> c:flush().
h(M) -> c:h(M).
h(M, F) -> c:h(M, F).
h(M, F, A) -> c:h(M, F, A).
ht(M) -> c:ht(M).
ht(M, F) -> c:ht(M, F).
ht(M, F, A) -> c:ht(M, F, A).
hcb(M) -> c:hcb(M).
hcb(M, F) -> c:hcb(M, F).
hcb(M, F, A) -> c:hcb(M, F, A).
i() -> c:i().
i(X, Y, Z) -> c:i(X, Y, Z).
pi(X, Y, Z) -> c:pi(X, Y, Z).
pi(Pid) -> c:pi(Pid).
l(Mod) -> c:l(Mod).
lc(X) -> c:lc(X).
ls() -> c:ls().
ls(S) -> c:ls(S).
m() -> c:m().
m(Mod) -> c:m(Mod).
lm() -> c:lm().
mm() -> c:mm().
memory() -> c:memory().
memory(Type) -> c:memory(Type).
nc(X) -> c:nc(X).
ni() -> c:ni().
nl(Mod) -> c:nl(Mod).
nregs() -> c:nregs().
pid(X, Y, Z) -> c:pid(X, Y, Z).
pwd() -> c:pwd().
q() -> c:q().
regs() -> c:regs().
uptime() -> c:uptime().
xm(Mod) -> c:xm(Mod).
y(File) -> c:y(File).
y(File, Opts) -> c:y(File, Opts).

% The debugger's interpreter commands; Clause programs have no debugger, so each reports the failed call.
iaa(Flag) -> calli(iaa, [Flag]).
iaa(Flag, Fnk) -> calli(iaa, [Flag, Fnk]).
ist(Flag) -> calli(ist, [Flag]).
ia(Pid) -> calli(ia, [Pid]).
ia(X, Y, Z) -> calli(ia, [X, Y, Z]).
ia(Pid, Fnk) -> calli(ia, [Pid, Fnk]).
ia(X, Y, Z, Fnk) -> calli(ia, [X, Y, Z, Fnk]).
ib(Mod, Line) -> calli(ib, [Mod, Line]).
ib(Mod, Fnk, Arity) -> calli(ib, [Mod, Fnk, Arity]).
ibd(Mod, Line) -> calli(ibd, [Mod, Line]).
ibe(Mod, Line) -> calli(ibe, [Mod, Line]).
iba(M, L, Action) -> calli(iba, [M, L, Action]).
ibc(M, L, Cond) -> calli(ibc, [M, L, Cond]).
ic() -> calli(ic, []).
ih() -> calli(help, []).
ii(Mod) -> calli(ii, [Mod]).
ii(Mod, Op) -> calli(ii, [Mod, Op]).
il() -> calli(il, []).
im() -> calli(im, []).
ini(Mod) -> calli(ini, [Mod]).
ini(Mod, Op) -> calli(ini, [Mod, Op]).
inq(Mod) -> calli(inq, [Mod]).
ip() -> calli(ip, []).
ipb() -> calli(ipb, []).
ipb(Mod) -> calli(ipb, [Mod]).
iq(Mod) -> calli(iq, [Mod]).
ir(Mod, Line) -> calli(ir, [Mod, Line]).
ir(Mod, Fnk, Arity) -> calli(ir, [Mod, Fnk, Arity]).
ir(Mod) -> calli(ir, [Mod]).
ir() -> calli(ir, []).
iv() -> calli(iv, []).

calli(F, Args) ->
    c:appcall(debugger, i, F, Args).

%% A function defined in the shell, called as shell_default:F(Args); otherwise undef.
'$handle_undefined_function'(Func, Args) ->
    case shell:get_function(Func, length(Args)) of
        undefined -> error_handler:raise_undef_exception(?MODULE, Func, Args);
        Fun when is_function(Fun, length(Args)) -> apply(Fun, Args)
    end.
