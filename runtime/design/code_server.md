# Code server and module function registry — manual review sketch

Status: generic native skeleton implemented in compilation step 11, 2026-09-25.
See [runtime builtin dispatch](../../docs/runtime.md#code-server-and-builtins) for the implemented
boundary. The remaining sections describe the wider proposed contract: typed
registrations, export listings and unload are not implemented; atom bindings
exist and publication/lookup is synchronized (plan 11 step 55,
[threads](../../docs/runtime.md#threads)). Native typed sketches are retained under
`runtime/include/unverified/`; there is no implemented `native_callable.hpp`.

- [callable.hpp](../include/clause/runtime/callable.hpp): generic targets,
  exact signature keys and one frozen `ModuleRegistry` per module.
- [code_server.hpp](../include/clause/runtime/code_server.hpp): native module
  publication, pinned generic resolution and code-image lifetime.

## Function registration

A `ModuleRegistry` is a noncopyable, nonmovable function table. Build it privately,
then transfer its `unique_ptr` through `ModuleDefinition::functions` to exactly one
`LoadedModule`. Publication freezes that same registry. `LoadedModule::functions()`
exposes a const reference; further additions, including through an old draft alias,
fail with `RegistryError::frozen`. Draft access and publication must not race.
Published lookup is immutable and may run concurrently on scheduler workers.

Each key is **function / arity / argument types**. `FunctionKey` owns the exact
function atom spelling, the Erlang argument count and one `std::type_index` per
argument in order. The current `ProcessContext` is always an explicit leading C++
parameter, excluded from the key and arity. Return types are fixed to `CallResult<Term>`
and never participate in lookup. Names are not case-folded or normalized.

The default registration is:

```cpp
using Callable = std::function<CallResult<Term>(ProcessContext &, std::span<const Term>)>;
registry.add(function_atom, arity, Callable{target});
```

Its key contains `arity` copies of `typeid(Term)`. No type list or conversion
registration is needed. The span is the calling interface for a fixed Erlang arity,
not permission to pass a different number of arguments. Zero arity has an empty
type sequence and receives an empty span.

An exact native specialization uses a typed std::function:

```cpp
registry.add(function_atom, TypedCallable<std::int64_t, Term>{target});
```

This infers arity two and the exact `(int64_t, Term)` type sequence. Any unqualified,
move-constructible value type is allowed, including user-defined types.
Reference/cv-qualified signature types are excluded because RTTI
would erase those distinctions. The callable body may borrow its value arguments
through const references for the duration of the call, but must not retain them.

Generic and typed all-Term registrations denote the same key. Registering both is
a duplicate, including at arity zero. The registry supplies a span view for default
lookup and a fixed-argument view for exact all-Term typed lookup; these only adapt
calling shape and forward Terms, without encoding or decoding them. Stored entries
must retain their exact std::function type behind erasure; never reinterpret-cast
function pointers or call a target through a mismatched signature.

`add()` rejects empty targets, duplicate keys, invalid function atom Terms, invalid
arities and allocation failures without changing existing registrations. Atom Terms
are read on their owner thread; store their independent spelling metadata rather
than process-local roots. All names in a module must belong to the same runtime as
its module atom. Type identities are local to the compatible runtime C++ ABI, not
persistent wire IDs or a cross-toolchain plugin ABI.

## Lookup and calling

`find(name, arity)` selects the all-Term entry by default and returns a borrowed
pointer to its `Callable`. `find_typed<Args...>(name)` selects only the exact type
sequence and returns a typed view forwarding to the single stored target. Captured target
state is shared, not copied into independent function instances. Both report
`function_not_found` when the requested signature is absent. `keys()` returns
canonical signatures; export listings deduplicate by name/arity and sort by name
and arity. Ordering of native RTTI types is only meaningful within one runtime.

A boxed integer is still a Term for selection. `int64_t` does not match `double`,
`vector<int64_t>` does not match `list<int64_t>`, and a mixed `(int64_t, Term)`
signature is not a wildcard. Lookup compares only the declared argument types.
If a typed lookup misses and the caller wants the generic entry, it explicitly
calls `find(name, arity)` and supplies already-prepared Term arguments. There is
no automatic fallback that could pass native values to a Term signature.

All targets return `CallResult<Term>`. Native code explicitly constructs its result
in the caller's heap. There is no automatic result encoding, including no implicit
`void`-to-`ok` conversion. Conversion utilities are deferred for later consideration.

A direct target call is ordinary C++: its caller must check span arity, Term
ownership/liveness and the result, and catch host exceptions at the runtime boundary.
`ResolvedFunction::call()` provides this checked boundary for default all-Term calls;
it reports bad_arity before entry and translates allocation/native exceptions.
A failed or throwing body is never retried through another signature.

Targets are bounded synchronous functions. The process continuation accounts for
reductions before invoking them; they cannot block on a future or await a mailbox.
The former virtual `Callable`, `CallFrame`, `prepare` and `NativeArguments` protocol
has been removed from this proposal. Cooperative generated-code entry and resumable
call integration remain separate compiler/runtime work; a synchronous std::function
return does not represent a suspended Erlang function.

## Module ownership and code lifetime

One runtime owns one `CodeServer`. It publishes modules by exact module name;
each module owns one registry, its code image and runtime-bound atom metadata.
`load()` validates non-null image/registry, module identity and the complete draft
before freezing and publishing it. A duplicate module name fails without replacing
the existing module. A failed load never publishes a partial module.

`CodeServer::resolve(module, function, arity)` selects the generic registration and
returns a `ResolvedFunction` that pins the module. Missing modules report
module_not_found; a missing generic registration reports function_not_exported,
even if typed specializations exist. The atom-name overload resolves the same keys
on the names' owner thread. For typed access, first retain `find_module()`'s shared
module handle, then use its `functions().find_typed<Args...>()`.

Directly returned function pointers and copied typed targets do **not** pin the
module. Keep the shared module handle alive through lookup, invocation and destruction
of copied targets/captures. Registry targets and captures must be destroyed before
their `CodeImage`, since their destructors may reside in its executable storage.
A draft's code image must likewise outlive its target construction and destruction.
Registrations may not capture process-local Terms or borrowed process contexts.
Mutable runtime-owned captures must support calls from multiple scheduler workers.

`unload()` removes the name from future lookups. Existing module handles and
ResolvedFunctions retain their original registry, atom bindings and code image.
A later load creates a new module instance with its own registry. This is lifetime
management, not a hot-upgrade/purge protocol. File loading, `on_load` and dynamic
library discovery remain deferred. Registry locks protect publication/removal only;
no callable body or capture destructor executes while a server lock is held.

Compiled atom constants are initialized through runtime AtomStorage before module
publication and then stay read-only. Each loaded instance keeps atom words in
compiler slot order, including duplicates; these one-word Terms pin no spellings.
Handles pin the code, while the runtime owns atom records and retires their global
slots at teardown ([atoms](../../docs/terms.md#atoms)).

## Illustrative API usage

This illustrates the future atom/typed API, not the implemented string-name skeleton:

```cpp
using namespace clause::runtime;

CallResult<Term> generic_size(ProcessContext &, std::span<const Term>);
CallResult<Term> native_size(ProcessContext &, std::vector<std::int64_t>);

auto functions = std::make_unique<ModuleRegistry>();
auto generic_added = functions->add(size_atom, 1, Callable{generic_size});
auto native_added = functions->add(size_atom,
    TypedCallable<std::vector<std::int64_t>>{native_size});
// Check both RegistryResults before publication.
ModuleDefinition definition{module_atom, CodeImage::linked(), std::move(functions)};
auto loaded = server.load(std::move(definition));
// Check CodeResult; retain the module until all copied targets have been destroyed.
auto module = loaded.value();
auto typed = module->functions().find_typed<std::vector<std::int64_t>>("size");
auto native_result = typed.value()(context, std::vector<std::int64_t>{1, 2});
auto generic = server.resolve("native_demo", "size", 1);
auto generic_result = generic->call(context, argument_terms);
```

The native body receives the vector directly. The generic body receives one Term
from `argument_terms`; neither call consults a conversion registry. Generic registration, duplicates, freeze, lifetimes and invocation now have behavior
tests. Exact native specialization and atom binding tests remain future work.
