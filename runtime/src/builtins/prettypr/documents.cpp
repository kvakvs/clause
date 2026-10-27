#include "../lists/list_walk.hpp"
#include "prettypr.hpp"
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

// prettypr's documents and contexts: reading document terms on first visit, building documents and contexts as OTP's
// helpers do, and the result terms. Added for prettypr (plan 21 step LIB77E3a).
namespace clause::runtime::builtins::prettypr {
using front_end::forms::element;
using front_end::forms::is_atom;

Engine::Engine(ProcessContext &context) : build_(context) {
    nil_ = leaf(0, build_.terms().cons(build_.integer(0), build_.nil()));
    const std::array space{build_.integer(' ')};
    space_ = leaf(1, build_.terms().cons(build_.integer(1), build_.list(space)));
}

Number Engine::number(const Term &term) {
    const auto value = small(term.word());
    if (!value) {
        lists::no_clause();
    }
    return *value;
}

// Arenas

Index Engine::make(Doc doc) {
    docs_.push_back(doc);
    return static_cast<Index>(docs_.size() - 1);
}

Index Engine::context(Ctx ctx) {
    ctxs_.push_back(ctx);
    return static_cast<Index>(ctxs_.size() - 1);
}

Index Engine::cell(Index doc, Index next) {
    cells_.push_back({doc, next});
    return static_cast<Index>(cells_.size() - 1);
}

// The document of a term, read when the rewriting first visits it; one per distinct term.
Index Engine::doc_of(const Term &term) {
    if (const auto found = read_.find(term.word()); found != read_.end()) {
        return found->second;
    }
    const auto index = make(Doc{.kind = DocKind::term, .term = term});
    read_.emplace(term.word(), index);
    return index;
}

const Doc &Engine::doc(Index index) {
    if (docs_[index].kind == DocKind::term) {
        Doc copy = docs_[index];
        read(copy);
        docs_[index] = copy;
    }
    return docs_[index];
}

// One level of a document term; anything else has no clause in OTP's rewrite/2.
void Engine::read(Doc &doc) {
    const auto &term = doc.term;
    if (term.is_atom()) {
        return read_atom(doc);
    }
    const auto tag = front_end::forms::tag_of(term);
    const auto size = term.is_tuple() ? term.tuple_size_unchecked() : 0;
    if (tag == "text" && size == 2) {
        read_text(doc);
    } else if (tag == "sep" && size == 4) {
        read_sep(doc);
    } else {
        read_node(doc, tag, size);
    }
}

// null, or empty (the document a failed fit becomes).
void Engine::read_atom(Doc &doc) {
    if (is_atom(doc.term, "null")) {
        doc.kind = DocKind::null;
    } else if (is_atom(doc.term, "empty")) {
        doc.kind = DocKind::empty;
    } else {
        lists::no_clause();
    }
}

// The records of other documents: one or two subdocuments and their numbers.
void Engine::read_node(Doc &doc, std::string_view tag, std::size_t size) {
    struct Shape {
        std::string_view tag;
        std::size_t size;
        DocKind kind;
    };

    static constexpr std::array SHAPES{
        Shape{"nest", 3, DocKind::nest},    Shape{"beside", 3, DocKind::beside},  Shape{"above", 3, DocKind::above},
        Shape{"union", 3, DocKind::union_}, Shape{"float", 4, DocKind::floating}, Shape{"fit", 2, DocKind::fit},
    };
    const auto shape = std::ranges::find_if(SHAPES, [&](const Shape &s) { return s.tag == tag && s.size == size; });
    if (shape == SHAPES.end()) {
        lists::no_clause();
    }
    const auto term = doc.term;
    doc = Doc{.kind = shape->kind};
    if (shape->kind == DocKind::nest) {
        doc.n = number(element(term, 1));
        doc.a = doc_of(element(term, 2));
        return;
    }
    doc.a = doc_of(element(term, 1));
    if (shape->kind == DocKind::floating) {
        doc.n = number(element(term, 2));
        doc.m = number(element(term, 3));
    } else if (size == 3) {
        doc.b = doc_of(element(term, 2));
    }
}

void Engine::read_text(Doc &doc) {
    const auto str = string(element(doc.term, 1));
    doc = Doc{.kind = DocKind::text, .str = str};
}

// #sep{ds, i, p}: the documents as cells, in order.
void Engine::read_sep(Doc &doc) {
    const auto items = lists::proper(element(doc.term, 1));
    const auto indentation = number(element(doc.term, 2));
    const auto par = element(doc.term, 3);
    if (!is_atom(par, "true") && !is_atom(par, "false")) {
        lists::no_clause();
    }
    Index list = NONE;
    for (auto at = items.size(); at > 0; --at) {
        list = cell(doc_of(items[at - 1]), list);
    }
    doc = Doc{.kind = DocKind::sep, .list = list, .n = indentation, .p = is_atom(par, "true")};
}

// A text's [Width | Characters].
Index Engine::string(const Term &whole) {
    if (!whole.is_cons()) {
        lists::no_clause();
    }
    return leaf(number(whole.head_unchecked()), whole);
}

Index Engine::leaf(Number width, const Term &whole) {
    strs_.push_back(Str{.width = width, .whole = whole, .chars = whole.tail_unchecked()});
    return static_cast<Index>(strs_.size() - 1);
}

// OTP's concat/2: an empty string gives the other one; otherwise [W1 + W2 | [Chars1 | Chars2]].
Index Engine::concat(Index s1, Index s2) {
    if (strs_[s1].empty()) {
        return s2;
    }
    if (strs_[s2].empty()) {
        return s1;
    }
    strs_.push_back(Str{.width = strs_[s1].width + strs_[s2].width, .left = s1, .right = s2});
    return static_cast<Index>(strs_.size() - 1);
}

// Document builders

Index Engine::text(Index str) { return make(Doc{.kind = DocKind::text, .str = str}); }

Index Engine::nest(Number n, Index d) { return n == 0 ? d : make(Doc{.kind = DocKind::nest, .a = d, .n = n}); }

Index Engine::pair(DocKind kind, Index a, Index b) { return make(Doc{.kind = kind, .a = a, .b = b}); }

Index Engine::sep(Index list, Number n, bool p) {
    return make(Doc{.kind = DocKind::sep, .list = list, .n = n, .p = p});
}

Index Engine::floating(Index d, Number h, Number v) {
    return make(Doc{.kind = DocKind::floating, .a = d, .n = h, .m = v});
}

Index Engine::fit(Index d) { return make(Doc{.kind = DocKind::fit, .a = d}); }

Index Engine::nil() { return text(nil_); }

// union(fit(horizontal([D | Ds])), vertical([D | [nest(N, D1) || D1 <- Ds]])), Ds and N the sep context's.
Index Engine::expand_sep(Index d, const Ctx &sep_ctx) {
    const auto horizontally = fit(horizontal(cell(d, sep_ctx.list)));
    return pair(DocKind::union_, horizontally, vertical(cell(d, nested(sep_ctx))));
}

// union(beside(fit(D), beside(hspace(), mksep([fit(D1) | Ds], N - 1, true))), above(D, nest(N, par([D1 | Ds])))),
// [D1 | Ds] and N the sep context's.
Index Engine::expand_par(Index d, const Ctx &sep_ctx) {
    const auto first = cells_[sep_ctx.list];
    const auto rest = sep(cell(fit(first.doc), first.next), sep_ctx.i - 1, true);
    const auto horizontally = pair(DocKind::beside, fit(d), pair(DocKind::beside, text(space_), rest));
    return pair(DocKind::union_, horizontally, pair(DocKind::above, d, nest(sep_ctx.i, sep(sep_ctx.list, 0, true))));
}

// foldr1 of beside(D1, beside(hspace(), D2)) over a nonempty list.
Index Engine::horizontal(Index list) {
    std::vector<Index> items;
    for (auto at = list; at != NONE; at = cells_[at].next) {
        items.push_back(cells_[at].doc);
    }
    auto result = items.back();
    for (auto at = items.size() - 1; at > 0; --at) {
        result = pair(DocKind::beside, items[at - 1], pair(DocKind::beside, text(space_), result));
    }
    return result;
}

// foldr1 of above/2 over a nonempty list.
Index Engine::vertical(Index list) {
    std::vector<Index> items;
    for (auto at = list; at != NONE; at = cells_[at].next) {
        items.push_back(cells_[at].doc);
    }
    auto result = items.back();
    for (auto at = items.size() - 1; at > 0; --at) {
        result = pair(DocKind::above, items[at - 1], result);
    }
    return result;
}

// [nest(N, D) || D <- Ds], Ds and N the sep context's.
Index Engine::nested(const Ctx &sep_ctx) {
    std::vector<Index> items;
    for (auto at = sep_ctx.list; at != NONE; at = cells_[at].next) {
        items.push_back(nest(sep_ctx.i, cells_[at].doc));
    }
    Index result = NONE;
    for (auto at = items.size(); at > 0; --at) {
        result = cell(items[at - 1], result);
    }
    return result;
}

// Context builders

Index Engine::best_nest(Number w, Number r, Number i) {
    return context(Ctx{.kind = CtxKind::best_nest, .w = w, .r = r, .i = i});
}

Index Engine::best_nest_or(Number w, Number r, Number i, Index d) {
    return context(Ctx{.kind = CtxKind::best_nest_or, .d = d, .w = w, .r = r, .i = i});
}

Index Engine::above_nest(Index d, Number i, Index c) {
    return context(Ctx{.kind = CtxKind::above_nest, .d = d, .c = c, .i = i});
}

Index Engine::beside_ctx(Index d, Index c) { return context(Ctx{.kind = CtxKind::beside, .d = d, .c = c}); }

Index Engine::text_beside(Index s, Index c) { return context(Ctx{.kind = CtxKind::text_beside, .s = s, .c = c}); }

Index Engine::sep_nest(Index list, Number i, bool p, Index c) {
    return context(Ctx{.kind = CtxKind::sep_nest, .list = list, .c = c, .i = i, .p = p});
}

Index Engine::fit_ctx(Index c) { return context(Ctx{.kind = CtxKind::fit, .c = c}); }

Index Engine::float_beside(Index d, Number h, Number v, Index c) {
    return context(Ctx{.kind = CtxKind::float_beside, .d = d, .c = c, .h = h, .v = v});
}

Index Engine::float_above_nest(Index d, Number h, Number v, Number i, Index c) {
    return context(Ctx{.kind = CtxKind::float_above_nest, .d = d, .c = c, .i = i, .h = h, .v = v});
}

// Result terms: the final document (null, empty, or a text possibly nested) under the completed lines, innermost
// first: nest(N, above(text(S), Rest)).
Term Engine::result_term() {
    auto result = doc_term(result_);
    for (auto at = lines_.size(); at > 0; --at) {
        const auto line = lines_[at - 1];
        const std::array above{build_.atom("above"_atom),
                               build_.tuple(std::array{build_.atom("text"_atom), str_term(line.str)}), result};
        result = build_.tuple(above);
        if (line.indentation != 0) {
            result = build_.tuple(std::array{build_.atom("nest"_atom), build_.integer(line.indentation), result});
        }
    }
    return result;
}

// A final document: null, empty, a text or a nested text.
Term Engine::doc_term(Index d) {
    const auto &found = doc(d);
    switch (found.kind) {
    case DocKind::null:
        return build_.atom("null"_atom);
    case DocKind::empty:
        return build_.atom("empty"_atom);
    case DocKind::text:
        return build_.tuple(std::array{build_.atom("text"_atom), str_term(found.str)});
    case DocKind::nest: {
        const auto n = found.n;
        const auto inner = doc_term(found.a);
        return build_.tuple(std::array{build_.atom("nest"_atom), build_.integer(n), inner});
    }
    default:
        return found.term;
    }
}

Term Engine::str_term(Index s) {
    const auto &str = strs_[s];
    if (!str.joined()) {
        return str.whole;
    }
    const auto width = str.width;
    return build_.terms().cons(build_.integer(width), chars_term(s));
}

// The characters of a string: the leaf's, or [Chars1 | Chars2], built bottom-up without recursion.
Term Engine::chars_term(Index s) {
    std::vector<std::pair<Index, bool>> pending{{s, false}};
    while (!pending.empty()) {
        const auto [at, children_done] = pending.back();
        pending.pop_back();
        if (!strs_[at].joined() || chars_.contains(at)) {
            continue;
        }
        if (children_done) {
            join_chars(at);
            continue;
        }
        pending.insert(pending.end(), {{at, true}, {strs_[at].left, false}, {strs_[at].right, false}});
    }
    return chars_of(s);
}

// The characters of a joined string whose parts are built.
void Engine::join_chars(Index s) {
    const auto &str = strs_[s];
    chars_.emplace(s, build_.terms().cons(chars_of(str.left), chars_of(str.right)));
}

Term Engine::chars_of(Index s) const { return strs_[s].joined() ? chars_.at(s) : strs_[s].chars; }
} // namespace clause::runtime::builtins::prettypr
