#include "../ets/tables.hpp"
#include "../memory/message_buffer.hpp"
#include "../runtime_state.hpp"
#include "executor.hpp"
#include <array>
#include <new>
#include <terms.hpp>
#include <utility>

// The end of an ETS table owner (plan 21 step LIB10B, docs/runtime.md#ets-tables), run by the executor under its
// lock before the owner's links and monitors are signalled.
namespace clause::runtime::detail {
namespace {
// {'ETS-TRANSFER', Table, FromPid, HeirData}, built in the heap of the ended owner; Table is the name of a named
// table and the identifier of any other.
TermResult<Term> transfer_message(ProcessContext &owner, const ets::Tables::Transfer &transfer) {
    TermFactory factory(owner);
    const auto tag = factory.atom("ETS-TRANSFER"_atom);
    const auto &table_of = *transfer.table;
    const auto table = table_of.options().named ? Term::from_word(table_of.name(), owner)
                                                : factory.reference_number(table_of.number());
    const std::array parts{tag, table, Term::from_word(pid_of(owner), owner),
                           transfer.data->term().copy_to(owner.heap())};
    std::vector<Term> fields;
    for (const auto &part : parts) {
        if (!part) {
            return std::unexpected(part.error());
        }
        fields.push_back(*part);
    }
    return factory.tuple(fields);
}
} // namespace

void Executor::release_tables(ProcessContext &process) {
    auto &tables = runtime_.ets_tables;
    const auto pid = pid_of(process);
    if (!tables.holds(pid)) {
        return;
    }
    const auto released = tables.release(pid, [&process](Word heir) {
        auto *found = find(process, heir);
        return found != nullptr && found != &process && !found->generated_calls().failure();
    });
    for (const auto &table : released.dropped) {
        for (auto &index : table->destroy()) {
            retired_tables_.push_back(std::move(index));
        }
    }
    for (const auto &transfer : released.transfers) {
        // A heir set without data ({heir, Pid}) inherits without a message, as in OTP.
        if (auto *heir = find(process, transfer.heir); heir != nullptr && transfer.data) {
            announce(*heir, transfer_message(process, transfer), process);
        }
    }
}

void Executor::free_retired(std::unique_lock<std::mutex> &lock) {
    if (retired_tables_.empty() && retired_contexts_.empty()) {
        return;
    }
    auto tables = std::exchange(retired_tables_, {});
    auto contexts = std::exchange(retired_contexts_, {});
    lock.unlock();
    tables.clear();
    for (auto &context : contexts) {
        auto &runtime = context->runtime();
        runtime.free_context(std::move(context));
    }
    contexts.clear();
    lock.lock();
    // The list keeps its capacity, so ending processes do not allocate it again.
    if (retired_contexts_.empty()) {
        retired_contexts_.swap(contexts);
    }
}

void Executor::announce(ProcessContext &heir, const TermResult<Term> &message, ProcessContext &owner) {
    if (!message || !busy(&heir)) {
        deliver(heir, message ? message->copy_to(heir.heap()) : message);
        return;
    }
    const auto buffered = MessageBuffer::copy_of(*message, owner);
    if (!buffered) {
        throw std::bad_alloc();
    }
    post(heir, *buffered);
}
} // namespace clause::runtime::detail
