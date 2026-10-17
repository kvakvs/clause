# Atom storage

Pattern/guard step 3 implements runtime-owned storage, validated UTF-8 spellings,
limits, deduplication, host ownership and module bindings. The authoritative contract
is [runtime atoms](../../docs/terms.md#atoms); the public project API is
[atoms.hpp](../include/clause/runtime/atoms.hpp).

This supersedes the 2026-09-20 review sketch. In particular, IDs use a process-wide
non-recycled namespace to reject foreign raw words; they are not dense per runtime.
Each runtime owns immutable `AtomValue` records in `names_` (spelling to
`unique_ptr`) behind a shared mutex ([threads](../../docs/runtime.md#threads)).
It publishes them into `detail::atom_table`, a process-wide index with lock-free
reads by atom number. Chunks of 4,096 slots stay for the process lifetime;
capacity is 2^28 numbers, bounded by the word's atom payload on 32-bit targets.
There are no per-runtime `index_` or `words_` maps. Failed reservations may
consume IDs, while failed insertion publishes no record. Per-runtime atom limits
are unchanged; no atom collector or compaction is implemented.

Admission accepts only records whose `AtomValue::owner` is that storage or null
(the immortal static `abi::v1::fixed_atom_spellings` records). Runtime teardown
retires its global slots before freeing the records. A Term contains only its
word: `atom_spelling()` and `atom_utf8()` read the global table and report
`expired_context` for retired atoms; fixed atoms remain readable. A borrowed
spelling lasts only until runtime teardown. Atom numbers never leave a node:
`term_to_binary` and disk forms write spellings, not numbers.

## Deferred collection constraints

Atom collection needs a runtime-wide safe point and complete root enumeration:
process heaps/continuations, mailbox/receive/transit data, host Terms, pinned code
names/literals, descriptor registries, builtins and native metadata. Process-heap
collection alone cannot prove an atom unreachable. Table indexes are storage,
not semantic roots; no reclamation may run before every root category and worker
handshake exists.

Collection must preserve every survivor's exact word and spelling bytes, never
renumber or recycle words, and update the spelling map and global slots
consistently. Reinterning a collected spelling requires a fresh identity. Reclamation may free table capacity
but cannot restore exhausted word identities. Concurrent creators, root/collection
races and retained values across compaction still need behavioral validation.
These constraints do not provide a collector or authorize a successful GC result.

## Historical review validation (2026-09-20)

The original declaration-only review recorded: standalone/combined C++23 syntax
with warnings as errors, option/API type assertions, clang-format, repository
clang-tidy, Lizard and local documentation links passed on macOS arm64. Lizard had
no function bodies to measure. That draft claimed no runtime behavior, full-build
gate, other-platform validation or commit. Current implementation evidence is
[the step-3 record](../../docs/validation.md#history).
