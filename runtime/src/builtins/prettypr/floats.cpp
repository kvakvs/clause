#include "prettypr.hpp"

// OTP's rewrite/2 rules for floating documents (plan 21 step LIB77E3a): floats move left and up past each other and
// align, and every other context drops the float wrapper.
namespace clause::runtime::builtins::prettypr {
void Engine::on_float() {
    const auto d = docs_[doc_];
    const auto ctx = ctxs_[ctx_];
    switch (ctx.kind) {
    case CtxKind::beside:
        return float_in_beside(d, ctx);
    case CtxKind::above_nest:
        return float_in_above(d, ctx);
    case CtxKind::fit:
        return next(floating(fit(d.a), d.n, d.m), ctx.c);
    case CtxKind::float_beside:
    case CtxKind::float_above_nest:
        return float_in_float(d, ctx);
    default: // all other contexts drop the float wrapper
        return next(d.a, ctx_);
    }
}

// The rule for a float under a beside or above_nest whose own context is a float: move left (a float beside to its
// right), align vertically, move up (a float above with a larger v), align horizontally, or none.
Engine::Move Engine::float_move(const Doc &doc, const Ctx &inner) {
    if (inner.kind == CtxKind::float_beside) {
        if (inner.h > doc.n) {
            return Move::left;
        }
        return inner.v != doc.m ? Move::vertical : Move::none;
    }
    if (inner.kind != CtxKind::float_above_nest) {
        return Move::none;
    }
    if (inner.v > doc.m) {
        return Move::up;
    }
    return inner.v == doc.m && inner.h != doc.n ? Move::horizontal : Move::none;
}

// A float beside D1.
void Engine::float_in_beside(const Doc &d, const Ctx &beside) {
    const auto inner = ctxs_[beside.c];
    const auto move = float_move(d, inner);
    if (move == Move::none) { // pass on
        return next(beside.d, float_beside(d.a, d.n, d.m, beside.c));
    }
    const auto self = floating(d.a, d.n, d.m);
    const auto other = floating(inner.d, inner.h, inner.v);
    switch (move) {
    case Move::left:
        return next(pair(DocKind::beside, self, pair(DocKind::beside, other, beside.d)), inner.c);
    case Move::vertical:
        return next(pair(DocKind::above, other, pair(DocKind::beside, self, beside.d)), inner.c);
    case Move::up: // all three aligned vertically
        return next(pair(DocKind::above, nest(inner.i, self), pair(DocKind::above, other, beside.d)), inner.c);
    default: // horizontally
        return next(pair(DocKind::beside, other, pair(DocKind::beside, self, beside.d)), inner.c);
    }
}

// A float above D1 (indented N): as beside, keeping the indentation where OTP keeps it.
void Engine::float_in_above(const Doc &d, const Ctx &above) {
    const auto inner = ctxs_[above.c];
    const auto move = float_move(d, inner);
    if (move == Move::none) { // pass on
        return next(above.d, float_above_nest(d.a, d.n, d.m, above.i, above.c));
    }
    const auto self = floating(d.a, d.n, d.m);
    const auto other = floating(inner.d, inner.h, inner.v);
    switch (move) {
    case Move::left: // the indentation is lost
        return next(pair(DocKind::beside, self, pair(DocKind::beside, other, above.d)), inner.c);
    case Move::vertical:
        return next(pair(DocKind::above, other, pair(DocKind::above, self, nest(above.i, above.d))), inner.c);
    case Move::up: // note the indentation
        return next(
            pair(DocKind::above, nest(inner.i, self), pair(DocKind::above, other, nest(above.i + inner.i, above.d))),
            inner.c);
    default: // horizontally
        return next(pair(DocKind::beside, other, pair(DocKind::above, self, nest(above.i, above.d))), inner.c);
    }
}

// A float in a float's context: swap, align, or drop the wrapper of the rightmost or lower one.
void Engine::float_in_float(const Doc &d, const Ctx &ctx) {
    const auto self = floating(d.a, d.n, d.m);
    const auto other = floating(ctx.d, ctx.h, ctx.v);
    if (ctx.kind == CtxKind::float_beside) {
        if (ctx.h > d.n) { // swap
            return next(pair(DocKind::beside, self, other), ctx.c);
        }
        if (ctx.v != d.m) { // align vertically
            return next(pair(DocKind::above, self, other), ctx.c);
        }
        return next(pair(DocKind::beside, other, d.a), ctx.c);
    }
    if (ctx.v > d.m) { // swap (note the indentation)
        return next(pair(DocKind::above, nest(ctx.i, self), other), ctx.c);
    }
    if (ctx.v == d.m && ctx.h != d.n) { // align horizontally
        return next(pair(DocKind::beside, self, other), ctx.c);
    }
    next(pair(DocKind::above, other, nest(ctx.i, d.a)), ctx.c);
}
} // namespace clause::runtime::builtins::prettypr
