#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "options.hpp"
#include <array>
#include <terms.hpp>

// The ets table builtins (plan 21 steps LIB10A, LIB10B, docs/builtins.md#ets): new/2, delete/1, whereis/1, all/0,
// rename/2, give_away/3 and setopts/2.
namespace clause::runtime::builtins::ets {
namespace {
using detail::Executor;

// The detached heir data of a heir option; null for a heir without data.
std::shared_ptr<const DetachedTerm> heir_data(ProcessContext &context, const Heir &heir) {
    if (!heir.data) {
        return nullptr;
    }
    return std::make_shared<const DetachedTerm>(need(DetachedTerm::copy(context, *heir.data)));
}

// A heir that is not alive is no heir, as in OTP.
bool live_heir(ProcessContext &context, const Heir &heir) {
    return heir.pid == self(context) || Executor::alive(context, heir.pid);
}

// new(Name, Options): the name of a named table, the identifier of any other.
Term new_table(ProcessContext &context, const AtomArgument &name, const Term &options) {
    const auto read = new_options(options);
    const auto identifier = need(TermFactory(context).make_reference());
    const auto number = need(detail::reference_number(identifier));
    auto table = std::make_shared<Table>(
        detail::ets::TableIdentity{.number = number, .name = name.term.word(), .owner = self(context)}, read.table);
    if (read.heir && live_heir(context, *read.heir)) {
        table->set_heir(read.heir->pid, heir_data(context, *read.heir));
    }
    if (!context.ets_tables().add(table)) {
        bad_argument();
    }
    return read.table.named ? name.term : identifier;
}

// delete(Table): the table and its objects are gone.
Term delete_table(ProcessContext &context, const TableArgument &argument) {
    const auto found = table(context, argument, Access::write);
    if (!context.ets_tables().remove(*found)) {
        bad_argument();
    }
    found->destroy();
    return atom(context, "true");
}

// whereis(Name): the identifier of the named table, or undefined.
Term whereis(ProcessContext &context, const AtomArgument &name) {
    const auto found = context.ets_tables().named(name.term.word());
    return found ? identifier(context, *found) : atom(context, "undefined");
}

// all(): every table, a named one by its name.
TermResult<Term> all(ProcessContext &context) {
    std::vector<Term> tables;
    for (const auto &table : context.ets_tables().all()) {
        tables.push_back(table_term(context, *table));
    }
    return TermFactory(context).list(tables);
}

// rename(Table, Name): the new name of a named table, the identifier of any other.
Term rename(ProcessContext &context, const TableArgument &argument, const AtomArgument &name) {
    const auto found = table(context, argument, Access::write);
    if (!context.ets_tables().rename(*found, name.term.word())) {
        bad_argument();
    }
    return found->options().named ? name.term : identifier(context, *found);
}

// A table given to a process that ended before it could see it is dropped with the rest of that process's tables.
void drop_orphans(ProcessContext &context, Word pid) {
    const auto released = context.ets_tables().release(pid, [](Word) { return false; });
    for (const auto &table : released.dropped) {
        table->destroy();
    }
}

// give_away(Table, Pid, GiftData): Pid owns the table and gets {'ETS-TRANSFER', Table, FromPid, GiftData}.
Term give_away(ProcessContext &context, const TableArgument &argument, const PidArgument &pid, const Term &gift) {
    const auto found = table(context, argument, Access::owner);
    const auto to = pid.term.word();
    if (to == self(context) || !Executor::alive(context, to)) {
        bad_argument();
    }
    context.ets_tables().give(*found, to);
    TermFactory factory(context);
    const std::array message{atom(context, "ETS-TRANSFER"), table_term(context, *found),
                             need(Term::from_word(self(context), context)), gift};
    if (const auto sent = Executor::send(context, to, need(factory.tuple(message))); !sent) {
        throw BuiltinFailure{.term = sent.error()};
    }
    if (!Executor::alive(context, to)) {
        drop_orphans(context, to);
    }
    return atom(context, "true");
}

// The changes one setopts/2 call asks for; each at most once.
struct Changes {
    std::optional<HeirChange> heir;
    std::optional<Protection> protection;
};

std::optional<Protection> protection(const Term &option) {
    if (option.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto name = spelling(option.tuple_element_unchecked(1));
    if (name == "public" || name == "protected" || name == "private") {
        return name == "public" ? Protection::public_access
                                : (name == "protected" ? Protection::protected_access : Protection::private_access);
    }
    return std::nullopt;
}

// Read one {heir, ...} or {protection, P} option into `changes`; badarg for anything else or a repeat.
void change(Changes &changes, const Term &option) {
    if (!option.is_tuple() || option.tuple_size_unchecked() < 2) {
        bad_argument();
    }
    const auto name = spelling(option.tuple_element_unchecked(0));
    if (name == "heir" && !changes.heir) {
        changes.heir = heir_change(option);
        if (changes.heir) {
            return;
        }
    } else if (name == "protection" && !changes.protection) {
        changes.protection = protection(option);
        if (changes.protection) {
            return;
        }
    }
    bad_argument();
}

// The changes of one option or a proper list of them.
Changes changes_of(const Term &options) {
    Changes changes;
    if (options.is_tuple()) {
        change(changes, options);
        return changes;
    }
    auto rest = options;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        change(changes, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return changes;
}

// setopts(Table, Opts): one option or a list of them.
Term setopts(ProcessContext &context, const TableArgument &argument, const Term &options) {
    const auto found = table(context, argument, Access::owner);
    const auto changes = changes_of(options);
    if (changes.heir) {
        const auto &heir = changes.heir->heir;
        const auto live = heir && live_heir(context, *heir);
        context.ets_tables().set_heir(*found, live ? heir->pid : 0, live ? heir_data(context, *heir) : nullptr);
    }
    if (changes.protection) {
        found->set_protection(*changes.protection);
    }
    return atom(context, "true");
}

constexpr std::array ETS_TABLE_BUILTINS{
    typed_entry<new_table>("ets", "new"),   typed_entry<delete_table>("ets", "delete"),
    typed_entry<whereis>("ets", "whereis"), typed_entry<all>("ets", "all"),
    typed_entry<rename>("ets", "rename"),   typed_entry<give_away>("ets", "give_away"),
    typed_entry<setopts>("ets", "setopts"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_table_builtins() noexcept { return builtins::ets::ETS_TABLE_BUILTINS; }
} // namespace clause::runtime
