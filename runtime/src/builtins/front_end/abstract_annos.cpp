#include "../collections/collections.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "abstract.hpp"
#include "front_end.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>

// The annotation walk of erl_parse (plan 21 step LIB31): modify_anno1/3's clauses picking each node's annotation
// and children, and first_anno/1 and last_anno/1 over it.
namespace clause::runtime::builtins::front_end::abstract {
namespace {
bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term element(const Term &tuple, std::size_t index) { return tuple.tuple_element_unchecked(index); }

// The elements from `first` on of a tuple.
std::vector<Term> elements_from(const Term &tuple, std::size_t first) {
    std::vector<Term> items;
    for (auto index = first; index < tuple.tuple_size_unchecked(); ++index) {
        items.push_back(element(tuple, index));
    }
    return items;
}

bool is_tuple_of(const Term &term, std::size_t size) { return term.is_tuple() && term.tuple_size_unchecked() == size; }

// {attribute, A, Name, Value}: records, specs and types walk their contents, other attributes only A.
Parts attribute_parts(const Term &node) {
    const auto name = element(node, 2);
    const auto value = element(node, 3);
    if (is_atom(name, "record") && is_tuple_of(value, 2)) {
        return {element(node, 1), {element(value, 1)}};
    }
    if ((is_atom(name, "spec") || is_atom(name, "callback")) && is_tuple_of(value, 2)) {
        return {element(node, 1), {element(value, 1)}};
    }
    const auto typed = is_atom(name, "type") || is_atom(name, "opaque") || is_atom(name, "nominal");
    if (typed && is_tuple_of(value, 3)) {
        return {element(node, 1), {element(value, 1), element(value, 2)}};
    }
    return {element(node, 1), {}};
}

bool is_one_of(const Term &tag, std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (is_atom(tag, name)) {
            return true;
        }
    }
    return false;
}

// {function, Name, Arity} has no annotation; {function, M, F, A} walks its three parts.
std::optional<Parts> function_parts(const Term &node, std::size_t size) {
    if (size == 3) {
        return Parts{};
    }
    if (size == 4) {
        return Parts{std::nullopt, elements_from(node, 1)};
    }
    return std::nullopt;
}

// The clauses before the generic {Tag, A, ...} ones: forms, form infos, fun clauses and typed fields.
std::optional<Parts> special_parts(const Term &node, std::size_t size) {
    const auto tag = element(node, 0);
    if (is_atom(tag, "function")) {
        return function_parts(node, size);
    }
    if (is_atom(tag, "attribute") && size == 4) {
        return attribute_parts(node);
    }
    if (size == 2 && is_one_of(tag, {"warning", "error", "eof"})) {
        return Parts{};
    }
    if ((size == 2 && is_atom(tag, "clauses")) || (size == 3 && is_atom(tag, "typed_record_field"))) {
        return Parts{std::nullopt, elements_from(node, 1)};
    }
    return std::nullopt;
}

// {Tag, A, E1, ...}: records skip their name, record fields their record name and field, bin_element its types.
Parts generic_parts(const Term &node, std::size_t size) {
    const auto tag = element(node, 0);
    auto children = elements_from(node, 2);
    if (is_atom(tag, "record") && size == 4) {
        children.erase(children.begin());
    } else if (is_atom(tag, "record") && size == 5) {
        children.erase(children.begin() + 1);
    } else if ((is_atom(tag, "record_field") || is_atom(tag, "bin_element")) && size == 5) {
        children.pop_back();
        if (is_atom(tag, "record_field")) {
            children.pop_back();
        }
    }
    return {element(node, 1), std::move(children)};
}
} // namespace

Parts parts_of(const Term &node) {
    if (node.is_cons()) {
        return {std::nullopt, {node.head_unchecked(), node.tail_unchecked()}};
    }
    if (!node.is_tuple()) {
        return {};
    }
    const auto size = node.tuple_size_unchecked();
    if (size < 2 || size > 6) {
        lists::no_clause();
    }
    if (auto special = special_parts(node, size)) {
        return std::move(*special);
    }
    return generic_parts(node, size);
}

bool location_lte(ProcessContext &context, const Term &left, const Term &right) {
    const string::Terms terms(context);
    const auto widen = [&](const Term &location) {
        return location.is_integer() ? terms.tuple(std::array{location, terms.integer(1)}) : location;
    };
    return collections::compare(widen(left), widen(right)) <= 0;
}

Term first_anno(ProcessContext &context, const Term &node) {
    if (!node.is_tuple() || node.tuple_size_unchecked() < 2) {
        bad_argument();
    }
    // OTP reads the locations only as the walk meets annotations, so a node without any is its own answer.
    auto first = element(node, 1);
    std::optional<Term> first_location;
    walk_annos(node, [&](const Term &anno) {
        const auto location = anno_location(context, anno);
        if (!first_location) {
            first_location = anno_location(context, first);
        }
        if (!location_lte(context, location, *first_location)) {
            return Visit::stop;
        }
        first = anno;
        first_location = location;
        return Visit::next;
    });
    return first;
}

Term first_location(ProcessContext &context, const Term &node) {
    return anno_location(context, first_anno(context, node));
}

Term last_anno(ProcessContext &context, const Term &node) {
    std::optional<Term> last;
    walk_annos(node, [&](const Term &anno) {
        if (!last || !location_lte(context, anno, *last)) {
            last = anno;
        }
        return Visit::next;
    });
    const auto found = last.value_or(string::Terms(context).atom("*"_atom));
    const auto end = anno_end_location(context, found);
    return is_atom(end, "undefined") ? found : anno_set_location(context, end, found);
}

namespace {
// The rebuild of replace_annos/3: modify_anno1/3's clauses with every annotation replaced by one.
class Reannotator final {
  public:
    Reannotator(ProcessContext &context, const Term &anno) : terms_(context), anno_(anno) {}

    Term node(const Term &node) const {
        if (node.is_cons()) {
            return list(node);
        }
        if (!node.is_tuple()) {
            return node;
        }
        const auto size = node.tuple_size_unchecked();
        if (size < 2 || size > 6) {
            lists::no_clause();
        }
        if (const auto special = special_node(node, size)) {
            return *special;
        }
        return rebuilt(node, {.first = 2, .annotated = true, .kept = generic_kept(node, size)});
    }

  private:
    // Which elements a rebuild walks: those from `first` on except `kept`; element 1 is replaced when `annotated`.
    struct Shape {
        std::size_t first = 1;
        bool annotated = false;
        std::vector<std::size_t> kept;
    };

    Term list(const Term &list) const {
        std::vector<Term> heads;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            heads.push_back(node(rest.head_unchecked()));
        }
        return terms_.list(heads, node(rest));
    }

    Term rebuilt(const Term &tuple, const Shape &shape) const {
        std::vector<Term> items;
        for (std::size_t at = 0; at < tuple.tuple_size_unchecked(); ++at) {
            const auto item = element(tuple, at);
            if (at == 1 && shape.annotated) {
                items.push_back(anno_);
            } else if (at < shape.first || std::ranges::find(shape.kept, at) != shape.kept.end()) {
                items.push_back(item);
            } else {
                items.push_back(node(item));
            }
        }
        return terms_.tuple(items);
    }

    // Records keep their names, record fields their record and field names, bin_element its type list.
    static std::vector<std::size_t> generic_kept(const Term &node, std::size_t size) {
        const auto tag = element(node, 0);
        if (is_atom(tag, "record")) {
            return {size == 4 ? 2U : 3U};
        }
        if (size == 5 && is_atom(tag, "record_field")) {
            return {3, 4};
        }
        if (size == 5 && is_atom(tag, "bin_element")) {
            return {4};
        }
        return {};
    }

    // The clauses before the generic {Tag, A, ...} ones.
    std::optional<Term> special_node(const Term &node, std::size_t size) const {
        const auto tag = element(node, 0);
        if (is_atom(tag, "function") && size <= 4) {
            return size == 3 ? node : rebuilt(node, {});
        }
        if (is_atom(tag, "attribute") && size == 4) {
            return attribute(node);
        }
        if (size == 2 && is_one_of(tag, {"warning", "error", "eof"})) {
            return node;
        }
        if (is_unannotated(tag, size)) {
            return rebuilt(node, {});
        }
        return std::nullopt;
    }

    // {clauses, Cs} and {typed_record_field, Field, Type}: parts walked, no annotation.
    static bool is_unannotated(const Term &tag, std::size_t size) {
        return (size == 2 && is_atom(tag, "clauses")) || (size == 3 && is_atom(tag, "typed_record_field"));
    }

    // Records, specs and types walk their contents, other attributes only their annotation.
    Term attribute(const Term &node) const {
        const auto name = element(node, 2);
        const auto value = element(node, 3);
        const auto typed = is_atom(name, "type") || is_atom(name, "opaque") || is_atom(name, "nominal");
        const auto paired = is_atom(name, "record") || is_atom(name, "spec") || is_atom(name, "callback");
        const auto walks = (paired && is_tuple_of(value, 2)) || (typed && is_tuple_of(value, 3));
        const std::array items{element(node, 0), anno_, name, walks ? rebuilt(value, {}) : value};
        return terms_.tuple(items);
    }

    string::Terms terms_;
    Term anno_;
};
} // namespace

Term replace_annos(ProcessContext &context, const Term &node, const Term &anno) {
    return Reannotator(context, anno).node(node);
}
} // namespace clause::runtime::builtins::front_end::abstract
