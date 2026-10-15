# Erlang/OTP source reference

Clause tracks the official `maint-29` branch. The reviewed pin lives in
[`references/otp-pin.cmake`](../references/otp-pin.cmake):

- Revision `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` (OTP 29.1.2, upstream
  commit dated 2026-10-14), refreshed on 2026-10-14.
- Installed oracle used for goldens and audits: OTP 29.1.1 / ERTS 17.1.

`references/otp` is an ignored checkout. Normal builds and tests use
project-owned goldens and need neither the checkout nor installed OTP.
Configuration and tests never fetch or advance the reference. Live audits are
opt-in with `-DCLAUSE_OTP_AUDITS=ON`; see
[fixture regeneration](../tests/fixtures/patternmatch/generated/README.md).

OTP source, copied helpers and OTP-generated headers never enter Git. Committed
test inputs are locally authored; committed goldens are observations of those
inputs. See [validation](validation.md#fixtures-and-provenance).

## Refresh procedure

Run at the start of OTP-dependent work.

1. Check `git -C references/otp status --short` and preserve local work. Fetch:
   `git -C references/otp fetch https://github.com/erlang/otp.git refs/heads/maint-29:refs/remotes/origin/maint-29`.
   Review `git -C references/otp log -1 origin/maint-29` and changes since the
   pin, especially grammar, scanner, preprocessor and corpus inputs.
2. Fast-forward the clean local branch: `git merge --ff-only origin/maint-29`
   (first setup: `git switch --create maint-29 --track origin/maint-29`). Use LF
   checkout bytes (`core.autocrlf=false`) so checksums match on every host; on
   an existing Windows checkout normalize only manifest-listed text files and
   `erl_parse.yrl`, and keep upstream binary fixtures unchanged.
3. Generate the compiler header from the same revision (from the project root):
   `perl references/otp/erts/emulator/utils/beam_makeops -compiler -outdir references/otp/lib/compiler/src references/otp/lib/compiler/src/genop.tab`.
   On Windows normalize only the generated `beam_opcodes.hrl` to LF.
4. Update the pin and verify every path in
   `tests/fixtures/parser/phase6/otp.tsv`. Refresh hashes only after reviewing
   real upstream changes. If `erl_parse.yrl` changes, review
   `tests/fixtures/parser/grammar.tsv`, its fixtures and `phase6/coverage.tsv`;
   every ordinary production needs a measured witness.
5. Configure with `CLAUSE_OTP_AUDITS=ON` and `CLAUSE_OTP_SOURCE_ROOT` set to the
   checkout. Run the audit tests and
   `tests/compiler/patternmatch/regenerate.py --corpus all --check`. Record the
   revision and outcomes below; report drift instead of regenerating to hide it.

## Pin history

Historical records keep the revision they were made against.

| Date | Event | Revision |
| --- | --- | --- |
| 2026-09-19 | Parser validation against OTP 29.1 tag | `751f87b703fe5948607d08e82599ce644b772e76` |
| 2026-09-28 | Switched to `maint-29`; grammar and ten corpus files identical | `21776803ecd11f5fa948732c0ec66b8f325dedfc` |
| 2026-10-01 – 10-03 | Re-fetched before each pattern/guard step; unchanged | same |
| 2026-10-03 | Plan 11 step 1: 14 audit tests pass, 19 corpora reproduce | same |
| 2026-10-14 | Refreshed to OTP 29.1.2: grammar, scanner, preprocessor and `phase6/otp.tsv` hashes unchanged (`genop.tab` comment only); 17 audit tests pass and 19 corpora reproduce under oracle 29.1.1; the revision recorded by executable, lint, program and printing goldens updated (outputs unchanged, `external_terms` keeps its atom-order-dependent map output) | `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` |
