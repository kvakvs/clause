#include "maps.hpp"
#include "service_errors.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/maps.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
using Op = abi::v1::MapOperation;
using Outcome = abi::v1::MapOutcome;

// Marshal only owned words; flags are canonical integers, so every input is a valid root term.
TermResult<std::vector<Term>> admit(ProcessContext &context, std::span<const Word> values) {
    std::vector<Term> terms;
    terms.reserve(values.size());
    for (const auto word : values) {
        const auto term = Term::from_word(word, context);
        if (!term) {
            return std::unexpected(term.error());
        }
        terms.push_back(*term);
    }
    return terms;
}

// Literal maps stage pairs and publish once; malformed marshalling is an infrastructure error.
MapResult make(ProcessContext &context, std::span<const Term> terms) {
    if (terms.size() % 2 != 0) {
        return std::unexpected(MapFault{TermError::invalid_encoding, {}});
    }
    MapEntries entries;
    entries.reserve(terms.size() / 2);
    for (std::size_t i = 0; i < terms.size(); i += 2) {
        entries.emplace_back(terms[i], terms[i + 1]);
    }
    return TermFactory(context).map(entries).transform_error([](TermError error) { return MapFault{error, {}}; });
}

// Exact flags preserve source order even when different key expressions resolve to the same key. An owned update's
// source map is read by nothing else (OPT0.5B).
MapResult update(ProcessContext &context, std::span<const Term> terms, bool owned) {
    if (terms.empty() || (terms.size() - 1) % 3 != 0) {
        return std::unexpected(MapFault{TermError::invalid_encoding, {}});
    }
    std::vector<MapUpdate> updates;
    updates.reserve((terms.size() - 1) / 3);
    for (std::size_t i = 1; i < terms.size(); i += 3) {
        const auto flag = terms[i + 2].integer_value();
        if (!flag || (*flag != 0 && *flag != 1)) {
            return std::unexpected(MapFault{TermError::invalid_encoding, {}});
        }
        updates.push_back({terms[i], terms[i + 1], *flag == 1});
    }
    return MapAccess::update(context.heap(), terms.front(), updates, owned);
}

// Queries validate the candidate type before searching exact keys; absence remains a distinct Erlang error.
MapResult query(ProcessContext &context, Op operation, std::span<const Term> terms) {
    const auto needed = operation == Op::get || operation == Op::contains ? 2U : 1U;
    if (terms.size() != needed) {
        return std::unexpected(MapFault{TermError::invalid_encoding, {}});
    }
    const auto &map = terms.front();
    const auto size = map.map_size();
    if (!size) {
        return std::unexpected(MapFault{size.error(), map});
    }
    if (operation == Op::test) {
        return map;
    }
    if (operation == Op::size) {
        return TermFactory(context).integer(static_cast<std::int64_t>(*size)).transform_error([&](TermError error) {
            return MapFault{error, map};
        });
    }
    const auto found = map.map_find(terms[1]);
    if (!found) {
        return std::unexpected(MapFault{found.error(), map});
    }
    if (operation == Op::contains) {
        return context.atom_storage().boolean(found->has_value()).transform_error([&](TermError error) {
            return MapFault{error, map};
        });
    }
    if (!*found) {
        return std::unexpected(MapFault{TermError::missing_key, terms[1]});
    }
    return **found;
}

// Build a map from a proper list of {Key, Value} tuples; later keys win.
MapResult from_list(ProcessContext &context, std::span<const Term> terms) {
    if (terms.size() != 1) {
        return std::unexpected(MapFault{TermError::invalid_encoding, {}});
    }
    MapEntries entries;
    auto rest = terms[0];
    while (rest.is_cons()) {
        const auto pair = rest.head().and_then([](const Term &tuple) { return tuple.tuple_elements(); });
        const auto next = rest.tail();
        if (!pair || pair->size() != 2 || !next) {
            return std::unexpected(MapFault{TermError::invalid_encoding, {}});
        }
        entries.emplace_back((*pair)[0], (*pair)[1]);
        rest = *next;
    }
    return TermFactory(context).map(entries).transform_error([](TermError error) { return MapFault{error, {}}; });
}

// The {Key, Value, Next} chain of OTP's map iterator from `position` on, ending in the atom none.
MapResult chain(ProcessContext &context, const Term &map, const std::size_t position) {
    TermFactory factory(context);
    auto result = factory.atom("none");
    for (auto index = map.map_size().value_or(0); result && index > position; --index) {
        result = map_entry(map, index - 1).and_then([&](const MapEntry &entry) {
            return factory.tuple(std::array{entry.first, entry.second, *result});
        });
    }
    return result.transform_error([&](TermError error) { return MapFault{error, map}; });
}

// Read the key or value at a canonical position, or the iterator chain from it.
MapResult position(ProcessContext &context, Op operation, std::span<const Term> terms) {
    if (terms.size() != 2) {
        return std::unexpected(MapFault{TermError::invalid_encoding, {}});
    }
    const auto &map = terms[0];
    const auto size = map.map_size();
    const auto index = terms[1].integer_value();
    if (!size || !index || *index < 0 || static_cast<std::uint64_t>(*index) > *size) {
        return std::unexpected(MapFault{TermError::invalid_encoding, map});
    }
    const auto at = static_cast<std::size_t>(*index);
    if (operation == Op::iterator) {
        return chain(context, map, at);
    }
    if (at == *size) {
        return std::unexpected(MapFault{TermError::invalid_encoding, map});
    }
    const auto entry = map_entry(map, at);
    if (!entry) {
        return std::unexpected(MapFault{entry.error(), map});
    }
    return operation == Op::key_at ? entry->first : entry->second;
}

// Keep construction and queries on one ownership-checked boundary without overloading integer opcodes.
MapResult evaluate(ProcessContext &context, Op operation, std::span<const Term> terms) {
    if (operation == Op::make) {
        return make(context, terms);
    }
    if (operation == Op::update || operation == Op::update_owned) {
        return update(context, terms, operation == Op::update_owned);
    }
    if (operation == Op::from_list) {
        return from_list(context, terms);
    }
    if (operation >= Op::key_at) {
        return position(context, operation, terms);
    }
    return query(context, operation, terms);
}

// Only semantic rejection may publish an error payload; resource/owner errors never modify output.
Outcome result(ProcessContext &context, const MapResult &value, Word *output) {
    if (value) {
        *output = value->word();
        return Outcome::success;
    }
    const auto &failure = value.error();
    if (failure.error == TermError::wrong_type || failure.error == TermError::missing_key) {
        *output = failure.payload.word();
        return failure.error == TermError::wrong_type ? Outcome::bad_map : Outcome::bad_key;
    }
    context.generated_calls().fail_service(term_status(failure.error));
    return Outcome::failure;
}

// Check pointer/count and active invocation state before dereferencing any borrowed argument array.
bool ready(ProcessContext &context, std::uint8_t operation, const Word *values, std::size_t count, Word *output) {
    auto &calls = context.generated_calls();
    if (!calls.active() || calls.failure()) {
        return false;
    }
    if (!output || (count != 0 && !values) || operation > static_cast<std::uint8_t>(Op::update_owned)) {
        calls.fail_service(abi::v1::Status::invalid_argument);
        return false;
    }
    return true;
}
} // namespace

// Contain all temporary marshalling and staged-update allocations at the generated service boundary.
std::uint8_t map_service(ProcessContext &context, std::uint8_t operation, const Word *values, std::size_t count,
                         Word *output) noexcept {
    if (!ready(context, operation, values, count, output)) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    try {
        const auto terms = admit(context, {values, count});
        if (!terms) {
            context.generated_calls().fail_service(term_status(terms.error()));
            return static_cast<std::uint8_t>(Outcome::failure);
        }
        return static_cast<std::uint8_t>(
            result(context, evaluate(context, static_cast<Op>(operation), *terms), output));
    } catch (const std::bad_alloc &) {
        context.generated_calls().fail_service(abi::v1::Status::out_of_memory);
    } catch (const std::length_error &) {
        context.generated_calls().fail_service(abi::v1::Status::resource_limit);
    } catch (...) {
        context.generated_calls().fail_service(abi::v1::Status::internal_error);
    }
    return static_cast<std::uint8_t>(Outcome::failure);
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_map_v1(void *opaque, std::uint8_t operation, const clause::abi::v1::TermWord *values,
                           std::size_t count, clause::abi::v1::TermWord *output) noexcept {
    using namespace clause;
    if (!opaque) {
        return static_cast<std::uint8_t>(abi::v1::MapOutcome::failure);
    }
    return runtime::detail::map_service(*static_cast<runtime::ProcessContext *>(opaque), operation, values, count,
                                        output);
}
