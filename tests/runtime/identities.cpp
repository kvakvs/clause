#include "scheduler/executor.hpp"
#include "terms.hpp"
#include "terms/structural_order.hpp"
#include <array>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

// Pid and reference identities (docs/terms.md#pids-and-references): admission accepts only pids this runtime issued
// and references in the process's own heap; forged, foreign and stale words are rejected, exited pids stay valid.
namespace {
using namespace clause::runtime;
using Status = clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Render a value in ~w style.
std::string text(const Term &value) { return format_term(value, TermStyle::write).value(); }

// Whether left orders before right in Erlang term order.
bool before(const Term &left, const Term &right) { return detail::structural_order(left, right).value() < 0; }

// The pid of a context's process.
Term pid(ProcessContext &context) { return TermFactory(context).pid(context.identity()).value(); }

// Issued pids print like OTP's, are admitted in every process of their runtime, and outlive their process.
void pids(Runtime &runtime) {
    auto &first = *runtime.create_context().value();
    auto &second = *runtime.create_context().value();
    const auto mine = pid(first);
    require(mine.is_pid() && !mine.is_reference() && std::regex_match(text(mine), std::regex("<0\\.[0-9]+\\.0>")),
            "pid text");
    require(format_term(mine, TermStyle::display).value() == text(mine), "pid display text");
    const auto seen = Term::from_word(mine.word(), second);
    require(seen && seen->exactly_equal(mine).value() && !pid(second).exactly_equal(mine).value(),
            "pids of one runtime are shared and distinct");
    const std::array fields{mine, pid(second)};
    require(TermFactory(second).tuple(fields).has_value() && second.heap().verify().has_value(),
            "a heap holding pids verifies");
    require(runtime.destroy_context(&first) == Status::ok && Term::from_word(mine.word(), second).has_value(),
            "the pid of an exited process stays valid");
    require(runtime.destroy_context(&second) == Status::ok, "teardown failed");
}

// Never-issued and foreign pid words are rejected wherever a word is admitted.
void forged_pids(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    auto other = Runtime::start().value();
    auto &foreign = *other->create_context().value();
    const auto mine = pid(context);
    const auto theirs = pid(foreign);
    const Word unissued = mine.word() + (Word{1} << 30);
    require(Term::from_word(unissued, context) == std::unexpected(TermError::wrong_owner) &&
                Term::from_word(Word{0x3}, context) == std::unexpected(TermError::wrong_owner),
            "forged pid admitted");
    require(Term::from_word(theirs.word(), context) == std::unexpected(TermError::wrong_owner) &&
                TermFactory(context).pid(foreign.identity()) == std::unexpected(TermError::wrong_owner),
            "foreign pid admitted");
    const std::array fields{theirs};
    require(TermFactory(context).tuple(fields) == std::unexpected(TermError::wrong_owner),
            "foreign pid stored in a tuple");
    require(Term::from_word(mine.word()) == std::unexpected(TermError::not_implemented),
            "a pid admitted without its runtime");
    require(other->destroy_context(&foreign) == Status::ok && runtime.destroy_context(&context) == Status::ok,
            "teardown failed");
}

// Ports opened in this runtime are admitted, survive closing as identities and copy unchanged; never-issued and
// foreign port words are rejected (docs/ports.md#identity).
void ports(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    auto &peer = *runtime.create_context().value();
    auto other = Runtime::start().value();
    auto &foreign = *other->create_context().value();
    auto &executor = detail::Executor::of(context);
    const auto port = Term::from_word(executor.open_port(context, detail::fd_driver(0, 1), {}, "0/1"), context).value();
    const auto theirs =
        Term::from_word(detail::Executor::of(foreign).open_port(foreign, detail::fd_driver(0, 1), {}, "0/1"), foreign)
            .value();
    require(port.is_port() && !port.is_pid() && std::regex_match(text(port), std::regex("#Port<0\\.[0-9]+>")),
            "port text");
    require(port.port_value().has_value() &&
                TermFactory(context).port(*port.port_value()).value().word() == port.word(),
            "port identity round trip");
    require(Term::from_word(port.word() + (Word{1} << 30), context) == std::unexpected(TermError::wrong_owner) &&
                Term::from_word(theirs.word(), context) == std::unexpected(TermError::wrong_owner),
            "forged or foreign port admitted");
    require(peer.heap().add(port).value().word() == port.word() && before(port, pid(context)) &&
                before(TermFactory(context).make_reference().value(), port),
            "port copy or order");
    require(executor.close_port(context, port.word()) && !executor.port_info(port.word()) &&
                Term::from_word(port.word(), peer).has_value(),
            "a closed port's identity is no longer valid");
    require(detail::Executor::of(foreign).close_port(foreign, theirs.word()), "foreign close failed");
    require(other->destroy_context(&foreign) == Status::ok && runtime.destroy_context(&peer) == Status::ok &&
                runtime.destroy_context(&context) == Status::ok,
            "teardown failed");
}

// References are unique, ordered by creation, survive collection and copying, and stale or foreign words fail.
void references(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    auto &peer = *runtime.create_context().value();
    TermFactory factory(context);
    const auto first = factory.make_reference().value();
    const auto second = factory.make_reference().value();
    require(first.is_reference() && !first.is_pid() &&
                std::regex_match(text(first), std::regex("#Ref<0\\.[0-9]+\\.[0-9]+\\.[0-9]+>")),
            "reference text");
    require(!first.exactly_equal(second).value() && before(first, second), "references are distinct");
    require(before(factory.atom("zzz").value(), first) && before(first, pid(context)) &&
                before(pid(context), factory.tuple({}).value()),
            "atoms < references < pids < tuples");
    require(Term::from_word(first.word(), peer) == std::unexpected(TermError::wrong_owner),
            "another process's reference admitted");
    const auto copy = first.copy_to(peer.heap()).value();
    require(copy.exactly_equal(first).value() && text(copy) == text(first), "a copied reference changed");
    const auto before_collection = text(first);
    std::array roots{first.word()};
    require(context.heap().collect(roots).has_value(), "collection failed");
    const auto moved = Term::from_word(roots[0], context).value();
    require(text(moved) == before_collection && moved.exactly_equal(copy).value(), "a collection changed a reference");
    require(context.heap().verify().has_value() && peer.heap().verify().has_value(), "heaps holding references");
    require(runtime.destroy_context(&context) == Status::ok && runtime.destroy_context(&peer) == Status::ok,
            "reference owners teardown failed");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        pids(*runtime);
        forged_pids(*runtime);
        ports(*runtime);
        references(*runtime);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
