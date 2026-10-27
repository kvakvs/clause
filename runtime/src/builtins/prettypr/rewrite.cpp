#include "../lists/list_walk.hpp"
#include "prettypr.hpp"
#include <tuple>
#include <utility>

// OTP's prettypr:rewrite/2, rule for rule (plan 21 step LIB77E3a): each step rewrites the current document in the
// current context; tail calls are the next step, and the one call OTP makes under a context (a completed line moved
// out, nest(N, above(text(S), rewrite(D1, ...)))) pushes a line instead. Comments name OTP's rule.
namespace clause::runtime::builtins::prettypr {
namespace {
// OTP's exit(badarg) for contexts that cannot occur.
[[noreturn]] void impossible() { throw BuiltinFailure{}; }
} // namespace

Term Engine::best(const Term &document, Number width, Number ribbon) {
    doc_ = doc_of(document);
    ctx_ = best_nest(width, ribbon, 0);
    while (result_ == NONE) {
        step();
    }
    return result_term();
}

// The next step rewrites document d in context c.
void Engine::next(Index d, Index c) { std::tie(doc_, ctx_) = std::pair{d, c}; }

void Engine::finish(Index d) { result_ = d; }

void Engine::push_line(Number indentation, Index str) { lines_.push_back({indentation, str}); }

void Engine::step() {
    switch (doc(doc_).kind) {
    case DocKind::text:
        return on_text();
    case DocKind::nest:
        return on_nest();
    case DocKind::above:
        return on_above();
    case DocKind::beside:
        return on_beside();
    case DocKind::sep:
        return on_sep();
    case DocKind::union_:
        return on_union();
    case DocKind::empty:
        return on_empty();
    case DocKind::fit:
        return on_fit();
    case DocKind::floating:
        return on_float();
    default:
        return on_null();
    }
}

// Texts

void Engine::on_text() {
    const auto s = docs_[doc_].str;
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::best_nest:
        return finish(nest(ctx.i, text(s)));
    case CtxKind::above_nest:
        return text_above(ctx, s);
    case CtxKind::beside:
        return text_beside_in(ctx, s);
    case CtxKind::text_beside:
        return next(text(concat(ctx.s, s)), ctx.c); // join segments
    case CtxKind::sep_nest:
        return text_in_sep(ctx, s);
    case CtxKind::best_nest_or:
        return text_in_or(ctx, s);
    case CtxKind::fit:
        return next(text(s), ctx.c);
    case CtxKind::float_beside:
        return next(pair(DocKind::beside, ctx.d, text(s)), ctx.c);
    case CtxKind::float_above_nest:
        return next(pair(DocKind::above, ctx.d, nest(ctx.i, text(s))), ctx.c);
    }
}

// A text above D1 (indented N1) in the context of the above_nest.
void Engine::text_above(const Ctx &above, Index s) {
    const auto inner = ctxs_[above.c];
    const auto rest = nest(above.i, above.d);
    switch (inner.kind) {
    case CtxKind::best_nest: // move out the completed line (note the new indentation)
        push_line(inner.i, s);
        return next(above.d, best_nest(inner.w - inner.i, inner.r, above.i));
    case CtxKind::beside: // associativity (not symmetric)
        return next(pair(DocKind::above, text(s), nest(above.i, pair(DocKind::beside, above.d, inner.d))), inner.c);
    case CtxKind::text_beside: // join segments (note the indentation)
        return next(pair(DocKind::above, text(concat(inner.s, s)), nest(above.i + strs_[inner.s].width, above.d)),
                    inner.c);
    case CtxKind::sep_nest:
        return text_above_sep(above, inner, s);
    case CtxKind::best_nest_or:
        return text_above_or(above, inner, s);
    case CtxKind::float_beside:
        return next(pair(DocKind::beside, inner.d, pair(DocKind::above, text(s), rest)), inner.c);
    case CtxKind::float_above_nest:
        return next(pair(DocKind::above, inner.d, nest(inner.i, pair(DocKind::above, text(s), rest))), inner.c);
    default:
        impossible();
    }
}

// A text above D1 in a sep: move out the prefix, or expand the sep when the text is empty.
void Engine::text_above_sep(const Ctx &above, const Ctx &inner, Index s) {
    if (!strs_[s].empty()) {
        const auto w = strs_[s].width;
        const auto first = pair(DocKind::above, nil(), nest(above.i - w, above.d));
        return next(pair(DocKind::beside, text(s), sep(cell(first, inner.list), inner.i - w, inner.p)), inner.c);
    }
    const auto first = pair(DocKind::above, nil(), nest(above.i, above.d));
    next(inner.p ? expand_par(first, inner) : expand_sep(first, inner), inner.c);
}

// A text above D1 under an alternative: select the alternative when the first line is not nice.
void Engine::text_above_or(const Ctx &above, const Ctx &inner, Index s) {
    if (too_wide(inner, s)) {
        return next(inner.d, best_nest(inner.w, inner.r, inner.i));
    }
    next(pair(DocKind::above, text(s), nest(above.i, above.d)), best_nest(inner.w, inner.r, inner.i));
}

// A text beside D1 in the context of the beside.
void Engine::text_beside_in(const Ctx &beside, Index s) {
    const auto inner = ctxs_[beside.c];
    switch (inner.kind) {
    case CtxKind::above_nest:
    case CtxKind::sep_nest:
        return text_beside_split(beside, inner, s);
    case CtxKind::text_beside: // associativity (simplified early)
        return next(pair(DocKind::beside, text(concat(inner.s, s)), beside.d), inner.c);
    case CtxKind::best_nest_or:
        if (too_wide(inner, s)) {
            return next(inner.d, best_nest(inner.w, inner.r, inner.i));
        }
        return next(beside.d, text_beside(s, beside.c));
    case CtxKind::float_beside:
        return next(pair(DocKind::beside, inner.d, pair(DocKind::beside, text(s), beside.d)), inner.c);
    case CtxKind::float_above_nest:
        return next(pair(DocKind::above, inner.d, nest(inner.i, pair(DocKind::beside, text(s), beside.d))), inner.c);
    default: // pass on
        return next(beside.d, text_beside(s, beside.c));
    }
}

// A nonempty text beside D1 above or in a sep moves out as a prefix (note the indentation); an empty one passes on.
void Engine::text_beside_split(const Ctx &beside, const Ctx &inner, Index s) {
    if (strs_[s].empty()) {
        return next(beside.d, text_beside(s, beside.c));
    }
    const auto w = strs_[s].width;
    const auto first = pair(DocKind::beside, nil(), beside.d);
    if (inner.kind == CtxKind::above_nest) {
        return next(pair(DocKind::beside, text(s), pair(DocKind::above, first, nest(inner.i - w, inner.d))), inner.c);
    }
    next(pair(DocKind::beside, text(s), sep(cell(first, inner.list), inner.i - w, inner.p)), inner.c);
}

// A text as a sep's first document: move out the prefix, or, for an empty text, the only union OTP introduces.
void Engine::text_in_sep(const Ctx &ctx, Index s) {
    if (!strs_[s].empty()) {
        const auto rest = sep(cell(nil(), ctx.list), ctx.i - strs_[s].width, ctx.p);
        return next(pair(DocKind::beside, text(s), rest), ctx.c);
    }
    next(ctx.p ? expand_par(nil(), ctx) : expand_sep(nil(), ctx), ctx.c);
}

// A text under an alternative: the alternative when it is not nice, else the finished layout.
void Engine::text_in_or(const Ctx &ctx, Index s) {
    if (too_wide(ctx, s)) {
        return next(ctx.d, best_nest(ctx.w, ctx.r, ctx.i));
    }
    finish(nest(ctx.i, text(s)));
}

// L + N > W orelse L > R.
bool Engine::too_wide(const Ctx &ctx, Index s) const {
    const auto width = strs_[s].width;
    return width + ctx.i > ctx.w || width > ctx.r;
}

// Nests, aboves, besides, seps

void Engine::on_nest() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::best_nest: // nest(N1, nest(N, D)) = nest(N1 + N, D)
        return next(d.a, best_nest(ctx.w, ctx.r, d.n + ctx.i));
    case CtxKind::above_nest: // distributivity (note the indentation)
        return next(nest(d.n, pair(DocKind::above, d.a, nest(ctx.i - d.n, ctx.d))), ctx.c);
    case CtxKind::beside: // associativity (not symmetric)
        return next(nest(d.n, pair(DocKind::beside, d.a, ctx.d)), ctx.c);
    case CtxKind::text_beside:
    case CtxKind::float_beside: // beside kills the indentation of its right side
        return next(d.a, ctx_);
    case CtxKind::sep_nest: // distributivity (the vertical form kills the indentation)
        return next(nest(d.n, sep(cell(d.a, ctx.list), ctx.i - d.n, ctx.p)), ctx.c);
    case CtxKind::fit:
        return next(nest(d.n, fit(d.a)), ctx.c);
    case CtxKind::float_above_nest:
        return next(d.a, float_above_nest(ctx.d, ctx.h, ctx.v, d.n + ctx.i, ctx.c));
    case CtxKind::best_nest_or:
        impossible();
    }
}

void Engine::on_above() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::above_nest: // associativity (note the indentation)
        return next(d.a, above_nest(pair(DocKind::above, d.b, nest(ctx.i, ctx.d)), 0, ctx.c));
    case CtxKind::beside: // associativity (not symmetric)
        return next(pair(DocKind::above, d.a, pair(DocKind::beside, d.b, ctx.d)), ctx.c);
    case CtxKind::fit: // the whole point of fit
        return next(make(Doc{.kind = DocKind::empty}), ctx.c);
    default:
        return next(d.a, above_nest(d.b, 0, ctx_));
    }
}

void Engine::on_beside() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::beside: // associativity
        return next(d.a, beside_ctx(pair(DocKind::beside, d.b, ctx.d), ctx.c));
    case CtxKind::fit: // distributivity
        return next(pair(DocKind::beside, fit(d.a), fit(d.b)), ctx.c);
    default:
        return next(d.a, beside_ctx(d.b, ctx_));
    }
}

void Engine::on_sep() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::fit: // only the horizontal layout, the indentation has no effect
        return next(fit(horizontal(d.list)), ctx.c);
    case CtxKind::float_beside: // floats are not moved in or out of seps
        return next(pair(DocKind::beside, ctx.d, sep(d.list, d.n, d.p)), ctx.c);
    case CtxKind::float_above_nest:
        return next(pair(DocKind::above, ctx.d, nest(ctx.i, sep(d.list, d.n, d.p))), ctx.c);
    default:
        return enter_sep(d);
    }
}

// A sep context never has an empty tail; a sep of no documents has no clause in OTP.
void Engine::enter_sep(const Doc &sep_doc) {
    if (sep_doc.list == NONE) {
        lists::no_clause();
    }
    const auto first = cells_[sep_doc.list];
    if (first.next == NONE) {
        return next(first.doc, ctx_);
    }
    next(first.doc, sep_nest(first.next, sep_doc.n, sep_doc.p, ctx_));
}

// Unions, empty, fit, null

void Engine::on_union() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::best_nest:
        return next(d.a, best_nest_or(ctx.w, ctx.r, ctx.i, d.b));
    case CtxKind::best_nest_or: // associativity
        return next(d.a, best_nest_or(ctx.w, ctx.r, ctx.i, pair(DocKind::union_, d.b, ctx.d)));
    default:
        return union_distributed(d, ctx);
    }
}

// Distributivity: union(X(D1), X(D2)) in the parent context, X the context's document.
void Engine::union_distributed(const Doc &d, const Ctx &ctx) {
    const auto wrap = [&](Index each) {
        switch (ctx.kind) {
        case CtxKind::above_nest:
            return pair(DocKind::above, each, nest(ctx.i, ctx.d));
        case CtxKind::beside:
            return pair(DocKind::beside, each, ctx.d);
        case CtxKind::text_beside:
            return pair(DocKind::beside, text(ctx.s), each);
        case CtxKind::sep_nest:
            return sep(cell(each, ctx.list), ctx.i, ctx.p);
        case CtxKind::fit:
            return fit(each);
        case CtxKind::float_beside:
            return pair(DocKind::beside, floating(ctx.d, ctx.h, ctx.v), each);
        default: // float_above_nest
            return pair(DocKind::above, floating(ctx.d, ctx.h, ctx.v), nest(ctx.i, each));
        }
    };
    const auto first = wrap(d.a);
    next(pair(DocKind::union_, first, wrap(d.b)), ctx.c);
}

void Engine::on_empty() {
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::best_nest: // preserve empty
        return finish(doc_);
    case CtxKind::best_nest_or: // try the other layout
        return next(ctx.d, best_nest(ctx.w, ctx.r, ctx.i));
    default: // preserve empty
        return next(doc_, ctx.c);
    }
}

void Engine::on_fit() {
    const auto d = docs_[doc_];
    if (ctxs_[ctx_].kind == CtxKind::fit) { // idempotency
        return next(d.a, ctx_);
    }
    next(d.a, fit_ctx(ctx_));
}

void Engine::on_null() {
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::best_nest: // done
        return finish(doc_);
    case CtxKind::above_nest:
        return next(nest(ctx.i, ctx.d), ctx.c);
    case CtxKind::beside:
        return next(ctx.d, ctx.c);
    case CtxKind::text_beside:
        return next(text(ctx.s), ctx.c);
    case CtxKind::sep_nest: // an empty document in a sep behaves like the empty string
        return next(nil(), ctx_);
    case CtxKind::best_nest_or: // as nice as it can be: the alternative goes
        return next(doc_, best_nest(ctx.w, ctx.r, ctx.i));
    case CtxKind::fit:
        return next(doc_, ctx.c);
    case CtxKind::float_beside: // the float wrapper goes
        return next(pair(DocKind::beside, ctx.d, doc_), ctx.c);
    case CtxKind::float_above_nest: // the empty document stays, to force line breaks later
        return next(pair(DocKind::above, ctx.d, nest(ctx.i, doc_)), ctx.c);
    }
}
} // namespace clause::runtime::builtins::prettypr
