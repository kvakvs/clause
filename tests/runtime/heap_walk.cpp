#include "memory/heap_walk.hpp"
#include "terms.hpp"
#include <array>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

// Heap parsing invariants that source cannot reach: every admitted layout walks, untraced payload is
// never a term, malformed areas are rejected and the verifier catches pointers to non-objects.
namespace {
using namespace clause::runtime;
using detail::HeapCell;
using detail::WalkError;
using detail::layout::BoxHeader;
using Shape = HeapCell::Shape;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Parse a synthetic area and return the cells, or the first error.
std::expected<std::vector<HeapCell>, WalkError> parse(std::span<const Word> area) {
    std::vector<HeapCell> cells;
    return detail::walk(area, [&](const HeapCell &cell) { cells.push_back(cell); }).transform([&] { return cells; });
}

// Build one heap holding every admitted layout, nested and shared, and check the exact census.
void every_layout() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    const auto atom = factory.atom("k").value();
    const auto one = factory.integer(1).value();
    // 2^62 + 1 needs a bignum whose low limb carries a list tag; the walker must not trace it.
    const auto pointer_like = factory.integer((std::int64_t{1} << 62) + 1).value();
    const auto negative = factory.integer_decimal("-" + std::string(400, '9')).value();
    const auto real = factory.floating(1.5).value();
    const auto proper = factory.list(std::array{one, one, one}).value();
    const auto improper = factory.list(std::array{atom}, one).value();
    const auto text = factory.list(std::array{factory.integer('h').value(), factory.integer('i').value()}).value();
    const auto tuple = factory.tuple(std::array{pointer_like, negative, real, proper, proper, improper}).value();
    const auto map = factory.map(std::array{std::pair{tuple, proper}, std::pair{atom, text}}).value();
    const auto empty = factory.map({}).value();
    const std::array<std::byte, 1> partial{std::byte{0xe0}};
    const auto bits = factory.bitstring(partial, 3).value();
    const auto inline_10 = factory.binary(std::vector(10, std::byte{1})).value();
    const auto inline_64 = factory.binary(std::vector(64, std::byte{2})).value();
    const auto shared = factory.binary(std::vector(100, std::byte{3})).value();
    const auto holder = factory.tuple(std::array{map, empty, bits, inline_10, inline_64, shared, map}).value();
    const auto census = context.heap().verify();
    require(census.has_value(), "valid heap rejected");
    require(census->cons_cells == 6 && census->boxed_objects == 11 && census->filler_words == 0,
            "census miscounted objects");
    require(census->words == context.heap().used_words() && census->off_heap_cells == 1, "census missed words");
    require(holder.tuple_size() == 7, "holder lost fields");
}

// Synthetic areas: shapes, slots, filler and every parse error.
void synthetic() {
    const auto small = encode_integer(5).value();
    const std::array tuple{BoxHeader::make(BoxedKind::tuple, 2), small, Word{0x3b}};
    const auto cells = parse(tuple).value();
    require(cells.size() == 1 && cells[0].shape == Shape::boxed && cells[0].slots.size() == 2, "tuple parse");
    const std::array fillers{Word{0}, BoxHeader::make(BoxedKind::filler, 2), Word{1}, Word{2}};
    const auto skipped = parse(fillers).value();
    require(skipped.size() == 2 && skipped[1].words.size() == 3 && skipped[1].slots.empty(), "filler parse");
    const std::array bignum{BoxHeader::make(BoxedKind::bignum, 2), Word{0}, Word{0x41}};
    require(parse(bignum).value()[0].slots.empty(), "bignum payload traced");
    const std::array cons{small, small};
    require(parse(cons).value()[0].shape == Shape::cons, "cons parse");
    const std::array odd_map{BoxHeader::make(BoxedKind::map, 3), small, small, small};
    require(parse(odd_map) == std::unexpected(WalkError::bad_size), "odd map accepted");
    const std::array binary{BoxHeader::make(BoxedKind::heap_binary, 3), Word{8}, Word{0}, Word{0}};
    require(parse(binary) == std::unexpected(WalkError::bad_size), "oversized heap binary accepted");
    const std::array unadmitted{BoxHeader::make(BoxedKind::match_context, 1), Word{0}};
    require(parse(unadmitted) == std::unexpected(WalkError::unknown_kind), "unadmitted kind accepted");
    // Another node's identity (plan 30 step DIST1): creation and numbers untraced, the node atom its one slot.
    const std::array short_external{BoxHeader::make(BoxedKind::ext_ref, 1), Word{0}};
    require(parse(short_external) == std::unexpected(WalkError::bad_size), "external reference without node");
    const std::array external{BoxHeader::make(BoxedKind::ext_pid, 4), Word{7}, Word{88}, Word{0}, Word{0x4b}};
    require(parse(external).value()[0].slots.size() == 1, "external pid node not traced");
    const std::array reference{BoxHeader::make(BoxedKind::reference, 3), Word{0}, Word{0}, Word{0}};
    require(parse(reference) == std::unexpected(WalkError::bad_size), "oversized reference accepted");
    const std::array short_tuple{BoxHeader::make(BoxedKind::tuple, 5), small};
    require(parse(short_tuple) == std::unexpected(WalkError::overrun), "tuple overrun accepted");
    const std::array half_cons{small};
    require(parse(half_cons) == std::unexpected(WalkError::overrun), "half cons accepted");
}

// Raw words written into a live heap: the verifier rejects pointers that do not reach object starts.
void corrupt() {
    auto runtime = Runtime::start().value();
    for (unsigned fault = 0; fault < 3; ++fault) {
        auto &context = *runtime->create_context().value();
        const auto target = TermFactory(context).tuple(std::array{Term::from_word(0x3b).value()}).value();
        auto words = context.heap().allocate(2).value();
        auto *slots = reinterpret_cast<Word *>(words.data());
        // An interior pointer, a list tag on a boxed object, and a header where a term belongs.
        const std::array bad{target.word() + sizeof(Word), (target.word() & ~Word{3}) | Word{1},
                             BoxHeader::make(BoxedKind::tuple, 1)};
        slots[0] = BoxHeader::make(BoxedKind::tuple, 1);
        slots[1] = bad[fault];
        require(context.heap().verify() == std::unexpected(HeapError::corrupt_heap), "corrupt slot accepted");
        slots[1] = target.word();
        require(context.heap().verify().has_value(), "repaired heap rejected");
        require(runtime->destroy_context(&context) == clause::abi::v1::Status::ok, "teardown failed");
    }
}
} // namespace

int main() {
    try {
        every_layout();
        synthetic();
        corrupt();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
