#include "proofs.hpp"
#include <algorithm>
#include <charconv>
#include <clause/abi/term.hpp>
#include <string_view>

namespace clause::codegen {
namespace {
using semantic::types::Kind;
using semantic::types::Node;

// A canonical decimal as an int64, or none when it does not fit.
std::optional<std::int64_t> decimal(const std::string_view text) {
    std::int64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return {};
    }
    return value;
}

// The members of a fact: a union's members, or the fact itself.
std::vector<semantic::types::Id> members(const semantic::types::Graph &graph, const semantic::types::Id fact) {
    const auto &node = graph.get(fact);
    return node.kind == Kind::union_type ? node.children : std::vector{fact};
}

// Whether a fact proves nothing: term(), or none() of code inference found unreachable.
bool vacuous(const semantic::types::Graph &graph, const semantic::types::Id fact) {
    return fact == graph.top() || fact == graph.bottom();
}

// The narrower of two list shapes, as the shape of a union holding both.
ListShape weaker(const ListShape left, const ListShape right) { return std::min(left, right); }
} // namespace

std::optional<SmallRange> Proofs::member_range(const Node &node) const {
    std::optional<std::int64_t> low;
    std::optional<std::int64_t> high;
    if (node.kind == Kind::integer) {
        low = high = decimal(node.name);
    } else if (node.kind == Kind::range && node.children.size() == 2) {
        low = decimal(graph_.get(node.children[0]).name);
        high = decimal(graph_.get(node.children[1]).name);
    }
    const auto minimum = bits_ == 32 ? abi::v1::IntegerEncoding<32>::minimum : abi::v1::IntegerEncoding<64>::minimum;
    if (!low || !high || *low < minimum || *high > -minimum - 1) {
        return {};
    }
    return SmallRange{*low, *high};
}

std::optional<SmallRange> Proofs::small(const semantic::types::Id fact) const {
    if (vacuous(graph_, fact)) {
        return {};
    }
    std::optional<SmallRange> result;
    for (const auto member : members(graph_, fact)) {
        const auto range = member_range(graph_.get(member));
        if (!range) {
            return {};
        }
        result = result ? SmallRange{std::min(result->low, range->low), std::max(result->high, range->high)} : range;
    }
    return result;
}

bool Proofs::floating(const semantic::types::Id fact) const {
    if (vacuous(graph_, fact)) {
        return false;
    }
    return std::ranges::all_of(members(graph_, fact), [&](const semantic::types::Id member) {
        const auto &node = graph_.get(member);
        return node.kind == Kind::application && node.name == "float" && node.children.empty();
    });
}

Known Proofs::normalized(const Known &known) const {
    const auto &node = graph_.get(known.fact);
    if (node.kind == Kind::positional && known.taken + 1 >= node.children.size()) {
        return {node.children.back(), 0};
    }
    return known;
}

std::optional<std::size_t> Proofs::arity(const Known &known) const {
    if (known.taken != 0 || vacuous(graph_, known.fact)) {
        return {};
    }
    std::optional<std::size_t> result;
    for (const auto member : members(graph_, known.fact)) {
        const auto &node = graph_.get(member);
        if (node.kind != Kind::tuple || node.name != "exact" || node.children.empty() ||
            (result && *result != node.children.size())) {
            return {};
        }
        result = node.children.size();
    }
    return result;
}

ListShape Proofs::member_list(const Node &node) const {
    if (node.kind == Kind::positional) {
        return ListShape::cons;
    }
    if (node.kind == Kind::list) {
        return node.name == "nonempty" ? ListShape::cons : ListShape::list;
    }
    if (node.kind != Kind::application) {
        return ListShape::unknown;
    }
    if (node.name == "nonempty_string" || node.name == "nonempty_list" || node.name == "nonempty_improper_list") {
        return ListShape::cons;
    }
    return node.name == "list" || node.name == "string" ? ListShape::list : ListShape::unknown;
}

ListShape Proofs::list(const Known &input) const {
    const auto known = normalized(input);
    if (vacuous(graph_, known.fact)) {
        return ListShape::unknown;
    }
    const auto &node = graph_.get(known.fact);
    if (known.taken != 0) {
        // The tail of a proper list is a proper list; a positional list still has a cell here (see normalized).
        return node.kind == Kind::positional ? ListShape::cons
                                             : (node.kind == Kind::list ? ListShape::list : ListShape::unknown);
    }
    auto result = ListShape::cons;
    for (const auto member : members(graph_, known.fact)) {
        result = weaker(result, member_list(graph_.get(member)));
    }
    return result;
}

std::optional<Known> Proofs::element(const Known &known, const std::size_t index) const {
    const auto size = arity(known);
    if (!size || index >= *size || graph_.get(known.fact).kind != Kind::tuple) {
        return {};
    }
    return Known{graph_.get(known.fact).children.at(index), 0};
}

std::optional<Known> Proofs::head(const Known &input) const {
    const auto known = normalized(input);
    if (vacuous(graph_, known.fact)) {
        return {};
    }
    const auto &node = graph_.get(known.fact);
    if (node.kind == Kind::positional) {
        return Known{node.children.at(known.taken), 0};
    }
    if (node.kind == Kind::list && !node.children.empty()) {
        return Known{node.children.front(), 0};
    }
    return {};
}

std::optional<Known> Proofs::tail(const Known &input) const {
    const auto known = normalized(input);
    if (vacuous(graph_, known.fact)) {
        return {};
    }
    const auto kind = graph_.get(known.fact).kind;
    if (kind == Kind::positional || kind == Kind::list) {
        return normalized(Known{known.fact, known.taken + 1});
    }
    return {};
}
} // namespace clause::codegen
