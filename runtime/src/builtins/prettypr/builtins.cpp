#include "../../terms/integers.hpp"
#include "../lists/list_walk.hpp"
#include "../typed.hpp"
#include "prettypr.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <vector>

// prettypr's builtins (plan 21 step LIB77E3a, docs/builtins.md#prettypr): best/3 runs the rewriting, and
// clause_prettypr's width/1, words/1 and layout/1 are OTP's strwidth/1, words/1 and layout/1. Malformed arguments
// raise function_clause, as OTP's clauses do.
namespace clause::runtime::builtins::prettypr {
namespace {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;
using Document = Named<struct DocumentRole>;
using Width = Named<struct WidthRole>;
using Ribbon = Named<struct RibbonRole>;
using Chars = Named<struct CharsRole>;
using Layout = Named<struct LayoutRole>;

constexpr std::int64_t TAB_STOP = 8;

bool is_char(const Term &term, char32_t c) {
    const auto value = small(term.word());
    return value && *value == static_cast<std::int64_t>(c);
}

// The word of a character.
Word character(char32_t c) { return need(Term::from_word(need(encode_integer(c)))).word(); }

// best(D, W, R).
Term best(ProcessContext &context, const Document &document, const Width &width, const Ribbon &ribbon) {
    Engine engine(context);
    return engine.best(document.term, Engine::number(width.term), Engine::number(ribbon.term));
}

// width(S): one column per element, a tab to the next multiple of 8.
Term width(ProcessContext &context, const Chars &text) {
    std::int64_t columns = 0;
    for (const auto &element : lists::proper(text.term)) {
        columns = is_char(element, U'\t') ? columns - (columns % TAB_STOP) + TAB_STOP : columns + 1;
    }
    return front_end::forms::Builder(context).integer(columns);
}

bool is_blank(const Term &term) { return is_char(term, U' ') || is_char(term, U'\t') || is_char(term, U'\n'); }

// words(S): the runs of elements between spaces, tabs and newlines.
Term words(ProcessContext &context, const Chars &text) {
    const front_end::forms::Builder build(context);
    std::vector<Term> found;
    std::vector<Term> word;
    for (const auto &element : lists::proper(text.term)) {
        if (!is_blank(element)) {
            word.push_back(element);
        } else if (!word.empty()) {
            found.push_back(build.list(word));
            word.clear();
        }
    }
    if (!word.empty()) {
        found.push_back(build.list(word));
    }
    return build.list(found);
}

// The characters of a deep string, flattened in order; [] elements vanish.
void flatten(const Term &chars, std::vector<Word> &out) {
    std::vector<Term> rest{chars};
    while (!rest.empty()) {
        const auto list = rest.back();
        rest.pop_back();
        if (list.is_nil()) {
            continue;
        }
        if (!list.is_cons()) {
            lists::no_clause();
        }
        const auto head = list.head_unchecked();
        rest.push_back(list.tail_unchecked());
        if (head.is_list()) {
            rest.push_back(head);
        } else {
            out.push_back(head.word());
        }
    }
}

// The characters of a #text{s = [Width | Characters]}; function_clause for anything else.
Term text_chars(const Term &text) {
    if (!is_node(text, "text", 2) || !element(text, 1).is_cons()) {
        lists::no_clause();
    }
    return element(text, 1).tail_unchecked();
}

// A line: the indentation, then the characters.
void put_line(const Term &chars, std::int64_t indentation, std::vector<Word> &out) {
    out.insert(out.end(), static_cast<std::size_t>(std::max<std::int64_t>(indentation, 0)), character(U' '));
    flatten(chars, out);
}

// layout(L): above(text, L) lines (an empty text's line is just a newline), nests adding indentation, a last text,
// or null.
Term layout(ProcessContext &context, const Layout &layout) {
    std::vector<Word> out;
    std::int64_t indentation = 0;
    auto rest = layout.term;
    while (!is_atom(rest, "null")) {
        if (is_node(rest, "above", 3)) {
            const auto chars = text_chars(element(rest, 1));
            if (!chars.is_nil()) {
                put_line(chars, indentation, out);
            }
            out.push_back(character(U'\n'));
            rest = element(rest, 2);
        } else if (is_node(rest, "nest", 3)) {
            indentation += Engine::number(element(rest, 1));
            rest = element(rest, 2);
        } else {
            put_line(text_chars(rest), indentation, out);
            break;
        }
    }
    TermFactory factory(context);
    return need(factory.list_words(out, need(factory.nil())));
}

constexpr std::array PRETTYPR_BUILTINS{
    typed_entry<best>("prettypr", "best"),
    typed_entry<width>("clause_prettypr", "width"),
    typed_entry<words>("clause_prettypr", "words"),
    typed_entry<layout>("clause_prettypr", "layout"),
};
} // namespace
} // namespace clause::runtime::builtins::prettypr

namespace clause::runtime {
std::span<const BuiltinEntry> prettypr_builtins() noexcept { return builtins::prettypr::PRETTYPR_BUILTINS; }
} // namespace clause::runtime
