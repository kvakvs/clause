#pragma once
#include "../front_end/forms.hpp"
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

// prettypr's layout chooser in C++ (plan 21 step LIB77E3a, docs/builtins.md#prettypr): OTP's rewrite/2, John Hughes'
// strict context-passing algorithm with OTP's additions (par, empty, float, null text), rule for rule, so best/3
// returns OTP's very layout documents, text strings included ([Width | Characters], joined as
// [W1 + W2 | [Chars1 | Chars2]]). Documents are read from terms on first visit and live in arenas with the contexts,
// so shared subdocuments cost nothing; the rewriting is a loop, and the lines it completes stack up as frames.
namespace clause::runtime::builtins::prettypr {
using Index = std::uint32_t;
inline constexpr Index NONE = UINT32_MAX;
using Number = std::int64_t;

// A text string: a leaf (an input [Width | Characters] term, or nil()/hspace()) or the concatenation of two strings.
struct Str {
    Number width = 0;
    Term whole{};
    Term chars{};
    Index left = NONE;
    Index right = NONE;

    bool joined() const { return left != NONE; }

    // OTP's is_empty_string/1: no characters at all (a null text has width 0 but characters).
    bool empty() const { return !joined() && chars.is_nil(); }
};

enum class DocKind : std::uint8_t { term, null, empty, text, nest, beside, above, sep, floating, union_, fit };

// A document. text: str; nest: n, a; beside/above/union: a, b; sep: list (cells), n = indentation, p = par;
// floating: a, n = h, m = v; fit: a; term: not read yet.
struct Doc {
    DocKind kind = DocKind::term;
    Term term{};
    Index a = NONE;
    Index b = NONE;
    Index str = NONE;
    Index list = NONE;
    Number n = 0;
    Number m = 0;
    bool p = false;
};

// A cell of a document list, as sep's documents are kept.
struct Cell {
    Index doc = NONE;
    Index next = NONE;
};

enum class CtxKind : std::uint8_t {
    best_nest,
    above_nest,
    beside,
    text_beside,
    sep_nest,
    best_nest_or,
    fit,
    float_beside,
    float_above_nest
};

// A context of OTP's rewrite/2 (#c_best_nest{w, r, i}, #c_above_nest{d, i, c}, ...), the fields it uses set.
struct Ctx {
    CtxKind kind = CtxKind::best_nest;
    Index d = NONE;
    Index s = NONE;
    Index list = NONE;
    Index c = NONE;
    Number w = 0;
    Number r = 0;
    Number i = 0;
    Number h = 0;
    Number v = 0;
    bool p = false;
};

class Engine final {
  public:
    explicit Engine(ProcessContext &context);

    // best(D, W, R) as a term: a layout document, or empty.
    Term best(const Term &document, Number width, Number ribbon);

    // Argument checks shared by the builtins: a small integer; function_clause for anything else.
    static Number number(const Term &term);

  private:
    // Arenas.
    Index doc_of(const Term &term);
    Index make(Doc doc);
    Index context(Ctx ctx);
    Index cell(Index doc, Index next);
    Index string(const Term &whole);
    Index leaf(Number width, const Term &whole);
    const Doc &doc(Index index);
    void read(Doc &doc);
    static void read_atom(Doc &doc);
    void read_text(Doc &doc);
    void read_sep(Doc &doc);
    void read_node(Doc &doc, std::string_view tag, std::size_t size);

    // Document builders, as OTP's helpers build them (nest(0, D) is D).
    Index text(Index str);
    Index nest(Number n, Index d);
    Index pair(DocKind kind, Index a, Index b);
    Index sep(Index list, Number n, bool p);
    Index floating(Index d, Number h, Number v);
    Index fit(Index d);
    Index nil();
    Index concat(Index s1, Index s2);
    Index expand_sep(Index d, const Ctx &sep_ctx);
    Index expand_par(Index d, const Ctx &sep_ctx);
    Index horizontal(Index list);
    Index vertical(Index list);
    Index nested(const Ctx &sep_ctx);

    // Context builders.
    Index best_nest(Number w, Number r, Number i);
    Index best_nest_or(Number w, Number r, Number i, Index d);
    Index above_nest(Index d, Number i, Index c);
    Index beside_ctx(Index d, Index c);
    Index text_beside(Index s, Index c);
    Index sep_nest(Index list, Number i, bool p, Index c);
    Index fit_ctx(Index c);
    Index float_beside(Index d, Number h, Number v, Index c);
    Index float_above_nest(Index d, Number h, Number v, Number i, Index c);

    // The loop: each step rewrites the current document in the current context.
    void step();
    void next(Index d, Index c);
    void finish(Index d);
    void push_line(Number indentation, Index str);
    void on_text();
    void text_above(const Ctx &above, Index s);
    void text_above_sep(const Ctx &above, const Ctx &inner, Index s);
    void text_above_or(const Ctx &above, const Ctx &inner, Index s);
    void text_beside_in(const Ctx &beside, Index s);
    void text_beside_split(const Ctx &beside, const Ctx &inner, Index s);
    void text_in_sep(const Ctx &ctx, Index s);
    void text_in_or(const Ctx &ctx, Index s);
    bool too_wide(const Ctx &ctx, Index s) const;
    void on_nest();
    void on_above();
    void on_beside();
    void on_sep();
    void enter_sep(const Doc &sep_doc);
    void on_union();
    void union_distributed(const Doc &doc, const Ctx &ctx);
    void on_empty();
    void on_fit();
    void on_float();
    void float_in_beside(const Doc &doc, const Ctx &beside);
    void float_in_above(const Doc &doc, const Ctx &above);
    void float_in_float(const Doc &doc, const Ctx &ctx);
    // Which of OTP's float rules applies to a float in a float's context.
    enum class Move : std::uint8_t { none, left, vertical, up, horizontal };
    static Move float_move(const Doc &doc, const Ctx &inner);
    void on_null();

    // Terms of the result.
    Term result_term();
    Term doc_term(Index d);
    Term str_term(Index s);
    Term chars_term(Index s);
    Term chars_of(Index s) const;
    void join_chars(Index s);

    front_end::forms::Builder build_;
    std::vector<Doc> docs_;
    std::vector<Str> strs_;
    std::vector<Cell> cells_;
    std::vector<Ctx> ctxs_;
    std::unordered_map<Word, Index> read_;
    std::unordered_map<Index, Term> chars_;

    // The lines moved out of the layout so far: nest(Indentation, above(text(Str), <the rest>)).
    struct Line {
        Number indentation = 0;
        Index str = NONE;
    };

    std::vector<Line> lines_;
    Index doc_ = NONE;
    Index ctx_ = NONE;
    Index result_ = NONE;
    Index nil_ = NONE;
    Index space_ = NONE;
};
} // namespace clause::runtime::builtins::prettypr
