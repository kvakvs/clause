# Executable golden cases

End-to-end cases for the executable golden runner
([plan 11 step 8](../../../.agents/11-plan.md#step-8)); the runner also runs the
[program fixtures](../programs/README.md) (step 58). Erlang sources are
linked into a program by `clau`, run with arguments, and stdout, exit
status and a stderr pattern are compared with an OTP-generated golden.

## A case

A directory holding the Erlang sources (`*.erl`, `*.hrl`) and `golden.json`.
A module whose `X.erl` needs a slow parse transform (`qlc.hrl`: Clause's
transform host takes minutes for it in a loaded Debug test run) also has
`X.abstr`, its forms after OTP's transform: the runner compiles the `.abstr`
instead of the `.erl`, and the oracle compiles the `.erl` (`qlc_queries`).
`transforms_no_otp` checks that the transform host gives the same forms.
Every such directory becomes the CTest `executables_<directory>`; nothing else
is registered.

| `golden.json` field | Written by | Meaning |
| --- | --- | --- |
| `schema` | author | Always 1 |
| `entry` | author | Entry module; `main/1` receives argv strings ([contract](../../../docs/executables.md)) |
| `data` | author, optional | Case-relative files the program uses at run time (helper scripts), staged beside the sources and hashed with them; runs start in the staged directory, and `CLAUSE_TEST_PYTHON` names the Python that runs the tests |
| `workers` | author, optional | Scheduler counts: every run is made once per count with `CLAUSE_FLAGS=--schedulers N`, instead of once with the default of one worker per logical processor ([workers](../../../docs/processes.md#workers)) |
| `options` | author, optional | Compiler options every policy adds (`shell_session`: `--keep-exports`, since the shell calls modules by name) |
| `stdout_masks` | author, optional | `[pattern, replacement]` pairs of multiline regular expressions applied to OTP's and Clause's stdout, for lines that differ by design (`shell_session`: the system version line) |
| `sources` | author, optional | Case-relative source paths instead of the directory's `.erl`/`.hrl`/`.abstr` files (the `demo` case uses `examples/compile/`) |
| `runs[].args` | author | Argument strings of one invocation |
| `runs[].stdin` | author, optional | Text written to standard input, which is then closed; empty when missing |
| `runs[].stderr` | author, or `^$` | Python regular expression searched in stderr; required when OTP writes stderr, because Clause's report text differs |
| `runs[].exit_status`, `runs[].stdout` | OTP | Observed under the pinned OTP with the program-fixture [oracle](../../compiler/programs/oracle.escript) |
| `runs[].env` | author, optional (authored runs only) | Environment entries for the run, such as `CLAUSE_FLAGS`; the runner always drops an inherited `CLAUSE_FLAGS` |
| `runs[].os` | author, optional | `windows` or `unix`: the run holds on that operating system family only and is skipped elsewhere; the oracle observes runs of its own family and keeps the others as written (`win32reg`) |
| `runs[].nodes` | author, optional | A multi-node run ([distribution testing](../../../docs/distribution.md#testing)) instead of `args`: every node starts at once on a fresh epmd whose port is in `ERL_EPMD_PORT`; each node has a `name`, `args`, a `stderr` pattern, optionally `kind` (`clause`, the default, or `otp`: the oracle runs it), and OTP's `exit_status` and `stdout` |
| `runs[].epmd`, `runs[].epmd_args` | author, optional | The epmd of a multi-node run: `clause-epmd` (default), `otp` (the host OTP's `epmd`) or `bundled` (none: the Clause nodes start their bundled epmd, which must end with its program), and its extra flags such as `-relaxed_command_check`; Clause nodes of the other runs get `--epmd none`; the oracle always uses OTP's `epmd` |
| `host_otp` | author, optional | `true` for a case whose runs need the host OTP at test time (OTP nodes or OTP's epmd): CTest label `host_otp`, skipped (exit 77) without an `escript` |
| `runs[].authored` | author, optional | `true` keeps the run's `exit_status`, `stdout` and `stderr` as written: Clause-only behavior OTP cannot show, such as the runtime options of `runtime_options` |
| `oracle_version`, `reference`, `inputs` | OTP | Oracle release, `maint-29` pin, and source hashes that make a stale golden fail before linking |

To add a case, write the sources and a golden with the author fields, then
generate it once (OTP is needed only here; erlfmt the sources first because the
hashes cover them):

```powershell
python tests/compiler/executables/regenerate.py --escript 'C:/Program Files/Erlang OTP/erts-17.1/bin/escript.exe' --case NAME
python tests/compiler/executables/regenerate.py --escript '...' --check   # writes nothing; also CTest executables_oracle
```

`--clau PATH` also rewrites every `X.abstr` from its `X.erl` with
`clau --print-abstr` and the oracle's `erl` (a header's `file` attribute, a
host path, is left out); give it when such an `.erl` changes.

Never regenerate to make a Clause comparison pass.

## Runner

`tests/compiler/executables/run.py <clau> <work> <case> --suffix=<.exe>`
stages the sources, links them under each combination of
`tests/compiler/patternmatch/matrix.py` (full mode: O0/O2 × specialization
on/off × positional `--entry … -o` and project manifest `entry`/`output`; fast
mode: O0 positional and O2 project, so inferred proofs run in fast mode too), runs every `runs[]` entry and prints a
unified stdout diff, exit-status and stderr-pattern mismatches.
`executables_selfcheck` proves a wrong golden fails with that diff and a stale
golden fails before linking. The tests need the runtime library and Clang, as
`linking_executable` does.
