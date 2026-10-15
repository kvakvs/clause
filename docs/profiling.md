# Profiling

`--profile FILE` (a [runtime option](executables.md#runtime-options), also
through `CLAUSE_FLAGS`) makes a program write a cost report to `FILE` when it
ends. Nothing is recompiled: every executable can profile, and without the
option it runs, prints and writes files exactly the same. A report that cannot
be written is reported on stderr (`clau: cannot write profile FILE`); the exit
status stays the program's.

## Attribution

The runtime attributes costs where control moves between frames
([execution model](execution-model.md)), with no instrumentation in generated
code:

- **Entries** (reductions): every entry of an Erlang function through a call or
  tail call counts one for that function, as a time slice counts reductions.
- **Self time**: steady-clock time from one transfer (entry, return, start of a
  time slice) to the next belongs to the function whose frame was on top.
  Builtins and runtime services count as time of the function that called them;
  time while a process waits or is not scheduled counts for nobody. Time before
  the first entry of a process is `(runtime)`.
- **Per process**: each process keeps its own costs; they are added to the
  program's when the process's context is destroyed, with its pid, totals and
  most expensive function.

Each enabled stack reads the clock at every transfer (about 8% slower on a
call-dense loop in Debug); disabled stacks pay one branch per transfer.

## Report

```text
Clause profile: 2 processes, 503 time slices, 0 timer clock readings
Functions by self time:
     time_us   share      entries  function
     1193261  100.0%      2000001  profile:spin/2
         204    0.0%            1  profile:main/1
         130    0.0%            1  profile:-main/1-fun-0-/1
           0    0.0%            1  profile:cold/1
Processes by time:
     time_us   share      entries  process: top function
     1193393  100.0%      2000002  <0.2.0>: profile:spin/2
         206    0.0%            2  <0.1.0>: profile:main/1
```

The first line also counts the time slices the scheduler workers ran and the
clock readings of the receive-timeout timer thread
([timer wheel](processes.md#receive-timeouts)). Functions are
`module:function/arity` (anonymous funs by their generated names, whose arity
counts captured values); ties sort by name, processes by time then pid. Times
are in microseconds and vary between runs; entries are exact.

## Tests

`linking_profiling` links `tests/fixtures/linking/profile` at O0 and O2 and runs
it without profiling, with `--profile FILE` and with
`CLAUSE_FLAGS=--profile=FILE`: the output is identical, `spin/2` ranks first by
self time with exactly 2,000,001 entries, the spinning process ranks first, and
`--profile` without a file fails with exit 70.
