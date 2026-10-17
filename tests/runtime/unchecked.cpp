#include "terms.hpp"
#include <array>
#include <clause/runtime/runtime.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

// The unchecked accessors (OPTM.3) agree with their checked mirrors on every layout of their kind: small and
// negative integers, inline and off-heap binaries, flat and tree maps, the empty tuple.
namespace {
using namespace clause::runtime;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Immediates and boxed scalars.
void scalars(TermFactory &factory) {
    for (const std::int64_t value : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{42}, std::int64_t{-(1LL << 40)}}) {
        const auto term = factory.integer(value).value();
        require(term.small_integer_unchecked() == term.integer_value().value(), "small integer differs");
    }
    const auto real = factory.floating(-2.5).value();
    require(real.float_value_unchecked() == real.float_value().value(), "float differs");
    for (const auto *spelling : {"true", "false", "unchecked_atom"}) {
        const auto atom = factory.atom(spelling).value();
        require(atom.atom_spelling_unchecked() == atom.atom_spelling().value() &&
                    atom.atom_id_unchecked() == atom.atom_id().value(),
                "atom differs");
    }
    require(factory.boolean(true)->boolean_value_unchecked() && !factory.boolean(false)->boolean_value_unchecked(),
            "boolean differs");
    const auto reference = factory.make_reference().value();
    require(reference.reference_value_unchecked() == reference.reference_value().value(), "reference differs");
}

// Lists and tuples.
void containers(TermFactory &factory) {
    const std::array items{factory.integer(1).value(), factory.atom("two").value(), factory.floating(3.0).value()};
    const auto list = factory.list(items).value();
    require(list.head_unchecked().word() == list.head()->word() && list.tail_unchecked().word() == list.tail()->word(),
            "cons fields differ");
    const auto tuple = factory.tuple(items).value();
    require(tuple.tuple_size_unchecked() == 3, "tuple size differs");
    for (std::size_t index = 0; index < items.size(); ++index) {
        require(tuple.tuple_element_unchecked(index).word() == tuple.tuple_element(index)->word(), "element differs");
    }
    require(factory.tuple({})->tuple_size_unchecked() == 0, "empty tuple size differs");
}

// Maps on both sides of the tree threshold, and bitstrings inline and off heap.
void sized(TermFactory &factory) {
    for (const std::size_t count : {std::size_t{0}, std::size_t{3}, std::size_t{100}}) {
        std::vector<std::pair<Term, Term>> entries;
        for (std::size_t index = 0; index < count; ++index) {
            const auto key = factory.integer(static_cast<std::int64_t>(index)).value();
            entries.emplace_back(key, key);
        }
        const auto map = factory.map(entries).value();
        require(map.map_size_unchecked() == map.map_size().value(), "map size differs");
    }
    for (const std::size_t bytes : {std::size_t{0}, std::size_t{10}, std::size_t{300}}) {
        const auto binary = factory.binary(std::vector(bytes, std::byte{1})).value();
        require(binary.bit_size_unchecked() == binary.bit_size().value(), "binary size differs");
    }
    const auto bits = factory.bitstring(std::vector(2, std::byte{0}), 13).value();
    require(bits.bit_size_unchecked() == 13, "bitstring size differs");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &context = *runtime->create_context().value();
        TermFactory factory(context);
        scalars(factory);
        containers(factory);
        sized(factory);
        require(runtime->destroy_context(&context) == clause::abi::v1::Status::ok, "teardown failed");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "runtime_unchecked: ok\n";
    return 0;
}
