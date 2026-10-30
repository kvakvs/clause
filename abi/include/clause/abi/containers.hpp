#pragma once
#include "immediate_services.hpp"
#include <cstddef>

namespace clause::abi::v1 {
// Lists carry their final tail as the last argument, so proper and improper spines share one constructor;
// reverse takes a proper list and the tail its elements are prepended to in reverse order.
enum class ContainerConstruction : std::uint8_t { tuple, list, reverse };
// Shape mismatch is a semantic outcome; ownership and infrastructure failures use the checked channel.
enum class ContainerInspection : std::uint8_t { tuple_shape, tuple_element, cons_shape, cons_head, cons_tail };
} // namespace clause::abi::v1

// Borrow rooted input words and publish a fully initialized result only on success.
std::uint8_t CLAUSE_construct_v1(void *context, std::uint8_t operation, const clause::abi::v1::TermWord *values,
                                 std::size_t count, clause::abi::v1::TermWord *output) noexcept;
// Write `tail` into the tail word of `cell`, a cons cell generated code built and holds alone, which lies outside
// FrameState's [young, heap_top): the next collection is then a full sweep, as an old cell may now point at a young
// term (forward-built lists, OPT1.12).
void CLAUSE_cons_tail_v1(void *context, clause::abi::v1::TermWord cell, clause::abi::v1::TermWord tail) noexcept;
// Prove ownership and shape before checking an arity/index or extracting an owned child.
std::uint8_t CLAUSE_inspect_v1(void *context, std::uint8_t operation, clause::abi::v1::TermWord value,
                               std::size_t index, clause::abi::v1::TermWord *output) noexcept;
