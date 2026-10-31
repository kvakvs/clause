# Architecture

**Update rule:** keep this file to architecture only: the sections of the project, what connects
where, which section owns what, and a one-line note of each section's public API. No module,
function, file, tuning or plan-history detail (that belongs in `files.md`, `docs/`, plan files and
`00-finished.md`). Edit in place, keep each bullet to 1-3 lines, compact when it grows.

Open work in `11-plan.md`, `20-opt.md`, `21-lib.md`; file locations in `files.md`.

## Overview

C++23, CMake, warnings as errors, project-internal APIs. One repository, three products sharing one
ABI:

```
 .erl/.hrl/.asn/.app ──► clau (compiler/) ──► LLVM objects ──► link ──► executable
                          │   ▲                                  ▲
                          │   └── library/ sources (on demand)   │
                          └── abi/include ◄──────────────────────┴── clause_runtime (runtime/)
                                                                      + extension archives
```

- `compiler/` → `clau`: frontend, driver/projects/caches, semantic analysis, type inference, LLVM
  lowering, backend, linking, ASN.1 compiler. Links LLVM.
- `abi/include` → generated-code ABI shared by compiler and runtime: frame/module/startup
  descriptors, literal encoding, builtin catalog, versioned `CLAUSE_*_v1` entry points.
- `runtime/` → `clause_runtime`, LLVM-free static archive, plus optional extension archives
  (`clause_runtime_crypto`, `clause_runtime_ssl`) linked only when a program needs them.
- `library/` → Clause's reimplementation of OTP applications (erts, kernel, stdlib, crypto, ssl,
  public_key, asn1, syntax_tools, ...) as Erlang sources; their C++ halves live in the runtime as
  builtins.
- `tests/` black-box CLI/executable tests with owned OTP-generated goldens; build and test need no
  OTP (`docs/validation.md`). `references/otp` is a transient reference checkout only.
- Stages exchange owned in-memory data; public interchange formats and stage readers are reserved
  directories only.

## 1. Compiler (`clau`)

Pipeline: source → preprocessor → parser → driver batch → semantic → inference → lowering → LLVM
backend → linking. Public API: the `clau` CLI (positional files or project manifests, `--print-*`
stage outputs, `--shell`, `--generate-asn1`), see `README.md` and `docs/compile.md`.

- **Frontend**: owns source buffers, lexing, preprocessing (macros, includes, conditionals) and
  parsing into owned AST arenas; source printing round-trips the AST. API: parser session → AST,
  `print_source`.
- **Parse transforms**: AST ↔ abstract-format terms (ETF). Transforms run in a Clause-built
  transform host (the library linked by `clau --shell`, cached per identity) that interprets
  project transform sources/`.beam`s; external `erl` is the fallback.
- **Driver / projects**: owns batches of targets, input closure (referenced modules pulled from
  the library directory, then project search paths), entry selection, predefined forms, generated
  `clause_applications` from `.app` resources, staged output publication and the SQLite cache
  (transform replies, compile results, inference results).
- **Semantic analysis**: binding/call resolution, guards, funs, records, liveness, behaviour checks.
  Produces per-module summaries consumed by inference and lowering.
- **Type inference**: implementation facts separate from declared specs; feeds specialization and
  inline fast paths in lowering.
- **Lowering (`codegen/`)**: AST + summaries → LLVM IR in the frame-based execution model
  (`docs/execution-model.md`). Calls into the runtime only through services declared in
  `codegen/runtime_symbols.hpp`, mangled per target (`mangling.hpp`). At O2 lowering itself inlines
  known funs, `lists`/`maps:fold` loops of them and small local functions calling only such leaves
  (budgeted per function): LLVM cannot inline across frame transfers. Matches of tuples/cons cells
  built in the same function read the built words, and unread cells are dropped (OPT1.13). Float
  loops (`<f>.float` variants taking raw doubles) are planned per module before any function is
  lowered, so calls between its functions pass doubles in registers (OPT1.11).
- **Backend**: LLVM optimization and emission (objects, asm, IR, debug info), parallel per module.
  Before the pipeline, term-word loads get the `term` TBAA tag (state/frame/binary tags come from
  lowering); atom/literal/fun cache reads are invariant loads, so their claim checks merge (OPT1.10).
  Emits a startup module handing a `StartupDescriptor` (module list + needed runtime extensions)
  to the runtime.
- **Linking**: drives Clang to link objects with the runtime archive, extension archives and their
  system libraries (OpenSSL found or built and cached).
- **ASN.1**: compiles ASN.1 specs into Erlang modules + `.hrl` that join the batch; the library's
  own specs are generated at build time into a second library root. Encoding/decoding itself is a
  runtime builtin driven by a schema term.

## 2. Runtime (`clause_runtime`)

Public API: ABI entry points called by generated code (`CLAUSE_main_v1`, enter/tail/return,
builtin and service calls), `RuntimeOptions` + `RuntimeExtension` for extension archives, the
`Term` model for C++ builtins. Owns everything below; never depends on LLVM or the compiler.

- **Execution**: per-process stack of frames, reduction-counted slices, first-error channel for
  Erlang errors vs infrastructure failures; startup parses runtime options and runs the entry.
- **Code server**: registered modules, exports, builtins, literal heaps, atom caches, interpreted
  modules (from `clause_interp`).
- **Terms**: one-word tagged terms (ERTS-like), all Erlang kinds, term order and printing. Pids, ports and
  references of other nodes are `ExternalIdentityCell`s (node atom traced last); this node's name and
  creation are process-wide (`detail::this_node_*`, plan 30 DIST1); `erlang:setnode/2` sets them and the
  executor's `node_owner_` (net_kernel) clears them when it ends (DIST2a, `docs/distribution.md#this-node`).
  The executor's `ConnectionTable` holds other nodes' entries and node status subscriptions (DIST2b,
  `scheduler/connections.cpp`): a controller's end takes its connection down, net_kernel's end kills the
  controllers and the name goes after the last one. Each entry holds its `monitor_node` monitors; an idle
  one auto-connects through net_kernel (DIST2c); a passive monitor continues in `kernel/clause_dist.erl`, which asks net_kernel.
  An up entry's `DataPath` (DIST2d) holds the output queue, input handler, `get_size` and counts; the `dist_ctrl_*`
  builtins (`builtins/erlang/connections.cpp`) reach it through `Executor::with_connections`.
- **epmd** (DIST3a): `runtime/src/epmd/` is a Boost.Asio epmd server (`registry` names, `requests` OTP's
  replies and checks, `server` listeners and connections) in the `clause_epmd_server` object library, run by
  the foreground `clause-epmd` and carried by `clause_runtime`. The bundled epmd (DIST3b, `epmd/bundled`) runs
  it on a thread of its own owned by the executor; `kernel/erl_epmd.erl` (client and registration server)
  asks `clause_dist:start_epmd/0` when a registration finds no epmd, under `--epmd bundled|none`.
  `clau --epmd-executable` (DIST3c) links `clause-epmd` (with `-daemon`, `-names`, `-kill`) for the target from
  the `clause_epmd_program` archive beside the runtime and publishes it beside the executable as a link companion.
  The bundled epmd lives in `clause_runtime_epmd` behind the startup extension entry `CLAUSE_epmd_extension_v1`
  (DIST3d; `--no-bundled-epmd` leaves both out, the executor creates it from `RuntimeExtension::bundled_epmd`);
  `--epmd external` finds `clause-epmd`/`epmd` in Erlang (`clause_dist`) and starts it detached.
- **Node start options** (DIST4a1): `startup/options.cpp` keeps `--sname`, `--setcookie`, ..., `--epmd` and
  `--env APP KEY VALUE` as init flags under OTP's names (`ProgramArguments::flags`, `init:get_argument/1`);
  `application_controller` reads `--env` from them as OTP's `get_cmd_env` reads `-App Key Value`, and
  distribution (DIST4b) reads the node flags as OTP's `net_kernel` does.
- **Starting distribution** (DIST4b1a): Erlang only. `net_kernel:start/1,2` starts `erl_distribution`'s `net_sup`
  unlinked (`erl_epmd`, `net_kernel`); `net_kernel`'s init names the node from the carrier's `listen/2`
  (`inet_tcp_dist`, epmd registration) with `erlang:setnode/2`, and its end takes the name (DIST2a).
- **Memory / GC**: per-process generational heaps, copying collector at safepoints, message and
  persistent-term copies, off-heap binaries (`docs/runtime-heap.md`).
- **Scheduler**: multi-worker executor with run queues, handed-over wakes and stealing, mailboxes,
  timers, signals, links/monitors (`docs/processes.md`).
- **Ports / I/O**: port table and drivers (fd, spawn, file, sockets) over one reactor thread
  (`docs/ports.md`).
- **ETS and match specs**: shared tables and compiled match programs.
- **Builtins**: the ABI builtin catalog shared with the compiler; C++ implementations of library
  functions grouped by OTP module family; long builtins yield in portions (`docs/builtins.md`).
- **Extensions**: crypto (OpenSSL libcrypto) and ssl engine (libssl) archives register their
  builtin families at startup when the startup descriptor lists them.

## 3. Library (`library/`, `docs/libraries/library.md`)

Public API: OTP 29-documented functions of each provided application, with OTP's observable
behavior (`docs/differences.md` lists gaps).

- **Ownership split**: C++ builtins (runtime) for data-heavy and hot code; Erlang for fun-taking
  functions, process protocols (behaviours, servers) and glue. Private `clause_*` modules bind the
  C++ halves to the public Erlang APIs.
- **Connection to compiler**: `clau` carries the library catalog (provided vs reserved-missing
  modules); library modules join a batch only when referenced; `erlang.erl` is generated from its
  inventory; application resources pull in their start modules and dependent applications.
- **Applications**: erts and kernel (processes, files, networking, code, applications), stdlib
  (text, collections, behaviours, logging, ETS-based structures, front-end tools: scanner, parser,
  linter, evaluator, module interpreter, `compile`), syntax_tools, crypto, public_key + asn1, ssl
  (one Erlang process per connection over the libssl engine extension; the `{handshake, hello}` pause
  reads the peer's hello before libssl does; libssl callbacks queue secrets/KeyUpdates/renegotiations as engine
  events the process takes after each step; TLS 1.2 sessions live in Erlang caches (`ssl_manager`, one per
  listener) as libssl DER, a server's ClientHello callback pausing for the lookup; TLS 1.3 client tickets in
  `tls_client_ticket_store`, stateful server tickets in the engine context; early data read before the handshake
  goes on, binder sets for anti_replay; sni_hosts/sni_fun pause the ClientHello and switch the server context;
  key_update_at/renegotiate_at counted in the connection's send path; CRL status checks in the verify fun over
  crl_cache modules; PSK keys from user_lookup_fun after the engine scans the peer's plaintext handshake). Distribution and tracing
  answer as a non-alive node.
