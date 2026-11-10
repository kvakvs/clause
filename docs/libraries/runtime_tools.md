# runtime_tools

Clause's `runtime_tools` (plan 21 MIM5) is the application's resource and start module (MIM6,
[OTP applications](library.md#otp-applications)) and the modules MongooseIM's dependencies call: `msacc`
(rabbit_common's `rabbit_runtime:msacc_stats/1`). The library is Erlang, in
[`library/runtime_tools/`](../../library/runtime_tools/).

## msacc

[`msacc`](../../library/runtime_tools/msacc.erl) is OTP's module over the runtime's microstate accounting
(`erlang:statistics(microstate_accounting)`, [system information](../builtins.md#system-information)):

- `available/0` is `true`; `start/0`, `stop/0` and `reset/0` switch and zero the accounting and return whether it
  was on; `start/1` counts from zero for the given milliseconds and stops (a bad time raises `timer:sleep/1`'s error
  with the accounting on, as OTP's).
- `stats/0` gives each thread's counters in microseconds (`#{'$type' => msacc_data, type, id, counters}`), ordered
  by type and id; Clause's threads are its workers, as `scheduler` threads.
- `stats/2` computes OTP's analyses: `system_realtime` and `system_runtime` (microseconds of all threads, with and
  without `sleep`), `realtime` and `runtime` (each state's share of its thread and of the system, in percent; 0.0 for
  an empty total), and `type` (counters summed per type, or shares averaged per type with system shares summed).
- `print/0,1,2,3` print OTP's tables: for counters the real-time and run-time averages (integer microseconds; no
  scheduler thread raises `badarith`), then each thread's and each type's shares, columns in OTP's state order
  (`emulator`, `port`, `aux`, `check_io`, `gc`, `other`, `sleep`), with `#{system => true}` the system share beside
  each; `print([])` raises `function_clause`.
- `to_file/1` writes `stats/0` as a term, `from_file/1` reads it back.

Checked by the `msacc_accounting` golden.
