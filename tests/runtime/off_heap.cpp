#include "memory/off_heap.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

// Off-heap binary ownership invariants that Erlang source cannot observe: buffer references held by
// refc_binary cells, the per-process off-heap list, budget rollback and the relocation hook.
namespace {
using namespace clause::runtime;
using detail::layout::BinaryBuffer;
using detail::layout::RefcBinaryCell;
using Op = clause::abi::v1::BitOperation;
using Status = clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Read the cell behind a boxed binary term; this test owns the context and the term is live.
const RefcBinaryCell &cell(const Term &value) {
    return *reinterpret_cast<const RefcBinaryCell *>(value.word() & ~Word{3});
}

// Read the sub-binary behind a slice (OPT1.4B).
const detail::layout::SubBinaryCell &range(const Term &value) {
    return *reinterpret_cast<const detail::layout::SubBinaryCell *>(value.word() & ~Word{3});
}

// Count the owner's off-heap list from its newest cell.
std::size_t list_length(const RefcBinaryCell *newest) {
    std::size_t count = 0;
    for (; newest != nullptr; newest = newest->next_) {
        ++count;
    }
    return count;
}

// Extract a sub-binary through the generated bit service, as source matching does.
Term slice(ProcessContext &context, const Term &value, std::size_t offset, std::size_t count) {
    TermFactory factory(context);
    const std::array input{value.word(), factory.integer(static_cast<std::int64_t>(offset))->word(),
                           factory.integer(256 + 2)->word(), factory.integer(static_cast<std::int64_t>(count))->word(),
                           factory.integer(0)->word()};
    std::array<Word, 2> output{};
    GeneratedInvocation call(context.generated_calls());
    require(CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(Op::extract), input.data(), input.size(),
                           output.data()) == 0,
            "slice failed");
    return Term::from_word(output[0], context).value();
}

// Tails are sub-binaries of the original's cell, so its buffer has one reference, charged once; a tail of a tail
// still names the original (OPT1.4B). Context teardown drops every reference even while host Terms remain.
void tails_and_teardown() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    const auto original = factory.binary(std::vector(100, std::byte{0x5a})).value();
    const auto small = factory.binary(std::vector(10, std::byte{0x5a})).value();
    const auto first = slice(context, original, 8, 700);
    const auto second = slice(context, first, 3, 600);
    std::weak_ptr<const BinaryBuffer> buffer = cell(original).buffer_;
    require(buffer.use_count() == 1, "tails took buffer references");
    require(range(second).original_ == original.word() && range(second).offset_ == 11 && range(second).bits_ == 600,
            "tail lost its original or offset");
    require(list_length(&cell(original)) == 1, "off-heap list does not hold exactly the original's cell");
    require(context.heap().off_heap_words() == (100 + sizeof(Word) - 1) / sizeof(Word), "buffer charged per tail");
    require(small.bit_size() == 80 && small.word() != 0, "inline binary failed");
    require(runtime->destroy_context(&context) == Status::ok, "teardown failed");
    // Host Terms do not pin heap storage: teardown drops every buffer reference at once.
    require(buffer.expired(), "teardown kept a buffer reference");
}

// A buffer that fits the budget but whose cell does not is uncharged and never listed.
void rollback() {
    constexpr auto buffer_words = (100 + sizeof(Word) - 1) / sizeof(Word);
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context({8, (buffer_words + 5) * sizeof(Word)}).value();
    TermFactory factory(context);
    require(factory.binary(std::vector(100, std::byte{1})) == std::unexpected(TermError::resource_limit),
            "cell beyond budget published");
    require(context.heap().off_heap_words() == 0 && context.heap().used_words() == 0 &&
                context.heap().capacity_words() == 0,
            "failed construction kept its charge");
    require(factory.binary(std::vector(1000, std::byte{1})) == std::unexpected(TermError::resource_limit),
            "buffer beyond budget charged");
    require(factory.binary(std::vector(20, std::byte{1}))->bit_size() == 160, "rollback poisoned inline retry");
    require(context.heap().off_heap_words() == 0, "inline binary charged off-heap words");
}

// The relocation hook moves the buffer reference; the source cell is left owning nothing.
void relocation() {
    auto buffer = std::make_shared<const BinaryBuffer>(100, std::byte{7});
    RefcBinaryCell source{};
    source.bits_ = 800;
    source.buffer_ = buffer;
    alignas(RefcBinaryCell) std::array<std::byte, sizeof(RefcBinaryCell)> storage{};
    auto &moved = detail::relocate_off_heap(source, storage.data());
    require(source.buffer_ == nullptr && moved.buffer_ == buffer && buffer.use_count() == 2,
            "relocation copied or lost the reference");
    require(moved.bits_ == 800, "relocation lost cell words");
    std::destroy_at(&moved);
    require(buffer.use_count() == 1, "relocated cell kept a reference");
}
} // namespace

int main() {
    try {
        tails_and_teardown();
        rollback();
        relocation();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
