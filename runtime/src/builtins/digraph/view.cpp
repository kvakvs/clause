#include "view.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// A digraph's tables read in place (plan 21 step LIB12): the stored terms the traversals need are copied into the
// caller's heap while their table part is locked.
namespace clause::runtime::builtins::digraph {
namespace {
using detail::ets::Index;

// The table in one field of a digraph record.
std::shared_ptr<ets::Table> table_at(ProcessContext &context, const Term &record, std::size_t at) {
    return ets::table(context, {record.tuple_element_unchecked(at)}, ets::Access::read);
}

bool digraph_record(const Term &record) {
    return record.is_tuple() && record.tuple_size_unchecked() == 5 && record.tuple_element_unchecked(0).is_atom() &&
           record.tuple_element_unchecked(0).atom_spelling_unchecked() == "digraph";
}

const Term &checked(const DigraphArg &digraph) {
    if (!digraph_record(digraph.term)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badrecord, .payload = digraph.term.word()};
    }
    return digraph.term;
}
} // namespace

DigraphView::DigraphView(ProcessContext &context, const DigraphArg &digraph)
    : context_(context), vertices_(table_at(context, checked(digraph), 1)), edges_(table_at(context, digraph.term, 2)),
      neighbours_(table_at(context, digraph.term, 3)) {}

Vertices DigraphView::vertices() const {
    Vertices found;
    ets::need_table(vertices_->read_parts([&](const Index &index) {
        index.each([&](const ets::Objects &objects) { found.push_back(ets::key_term(context_, *vertices_, objects)); });
    }));
    return found;
}

Vertices DigraphView::edges(const Term &vertex, const AtomName side) const {
    const std::array parts{atom(context_, side), vertex};
    const auto key_term = need(TermFactory(context_).tuple(parts));
    const auto at = ets::key(context_, *neighbours_, key_term);
    Vertices found;
    ets::need_table(neighbours_->read(at, [&](const Index &index) {
        const auto *objects = index.find(at);
        if (objects == nullptr) {
            return;
        }
        for (const auto &object : *objects) {
            found.push_back(ets::copy_out(context_, object.term().tuple_element_unchecked(1).word()));
        }
    }));
    return found;
}

Term DigraphView::edge_element(const Term &edge, std::size_t position) const {
    const auto at = ets::key(context_, *edges_, edge);
    std::optional<Term> element;
    ets::need_table(edges_->read(at, [&](const Index &index) {
        if (const auto *objects = index.find(at)) {
            element = ets::copy_out(context_, objects->front().term().tuple_element_unchecked(position - 1).word());
        }
    }));
    if (!element) {
        bad_argument();
    }
    return *element;
}

Vertices DigraphView::neighbours(const Term &vertex, std::string_view side, std::size_t position) const {
    Vertices found;
    for (const auto &edge : edges(vertex, side)) {
        found.push_back(edge_element(edge, position));
    }
    std::ranges::reverse(found);
    return found;
}

std::vector<std::pair<Term, Term>> DigraphView::out_ends(const Term &vertex) const {
    std::vector<std::pair<Term, Term>> found;
    for (const auto &edge : edges(vertex, "out"_atom)) {
        found.emplace_back(edge_element(edge, 2), edge_element(edge, 3));
    }
    return found;
}

std::size_t DigraphView::vertex_count() const {
    const auto size = vertices_->size();
    ets::need_table(size.has_value());
    return *size;
}

std::size_t DigraphView::edge_count() const {
    const auto size = edges_->size();
    ets::need_table(size.has_value());
    return *size;
}
} // namespace clause::runtime::builtins::digraph
