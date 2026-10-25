#include "../../match_spec/functions.hpp"
#include "../../terms/records.hpp"
#include "eval.hpp"
#include "guard_tests.hpp"
#include "lint.hpp"
#include <array>
#include <new>

// erl_eval's pattern matching in C++ (plan 21 step LIB34C, docs/builtins.md#erl-eval): match1/5 and the binary
// pattern and generator walks of library/stdlib/erl_eval.erl, with map keys and segment sizes evaluated over the
// outer bindings, and partial_eval/1 and is_constant_expr/1.
namespace clause::runtime::builtins::front_end::eval {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// The pattern does not match the term.
struct NoMatch {};

// The pattern is illegal ({illegal_pattern, Pattern} once erl_eval.erl raises it).
struct Invalid {};

// A pattern and the term it is matched against.
struct Subject {
    Term pattern;
    Term term;
};

// A part of a binary pattern (a bin_element, its string, a field list) and the bits it is taken from.
struct Piece {
    Term part;
    Term bits;
};

// What one binary generator segment did: matched, did not match (its bits taken all the same), or could not be taken.
enum class Taken : std::uint8_t { match, nomatch, done };

struct Step {
    Taken taken;
    Term rest;
};

bool any_failure_but_memory(const std::exception_ptr &failure) {
    try {
        std::rethrow_exception(failure);
    } catch (const std::bad_alloc &) {
        return false;
    } catch (...) {
        return true;
    }
}

class Matcher final {
  public:
    Matcher(ProcessContext &context, const Term &outer) : build_(context), outer_(build_, outer) {}

    // match1/5: Failed, NoMatch or Invalid thrown, `bindings` extended on a match.
    void pattern(const Subject &subject, Bindings &bindings) {
        using Rule = void (Matcher::*)(const Subject &, Bindings &);

        struct Clause {
            std::string_view tag;
            std::size_t size;
            Rule rule;
        };

        static constexpr auto CLAUSES = std::to_array<Clause>({
            {"atom", 3, &Matcher::literal},
            {"integer", 3, &Matcher::literal},
            {"float", 3, &Matcher::literal},
            {"char", 3, &Matcher::literal},
            {"string", 3, &Matcher::literal},
            {"var", 3, &Matcher::variable},
            {"match", 4, &Matcher::both},
            {"nil", 2, &Matcher::nil},
            {"cons", 4, &Matcher::cons},
            {"tuple", 3, &Matcher::tuple},
            {"map", 3, &Matcher::map},
            {"record", 4, &Matcher::record},
            {"bin", 3, &Matcher::binary},
            {"op", 4, &Matcher::constant},
            {"op", 5, &Matcher::operation},
        });
        const auto &p = subject.pattern;
        const auto tag = forms::tag_of(p);
        const auto size = p.is_tuple() ? p.tuple_size_unchecked() : 0;
        for (const auto &clause : CLAUSES) {
            if (clause.tag == tag && clause.size == size) {
                (this->*clause.rule)(subject, bindings);
                return;
            }
        }
        throw Invalid{};
    }

    // bits_gen/6 from the front of `bits`: {match, Rest, Bindings}, {nomatch, Rest} or done.
    Term generate(const Piece &fields, Bindings &bindings) {
        auto rest = fields.bits;
        auto matched = true;
        for (const auto &field : forms::elements(fields.part)) {
            const auto step = generated_segment({field, rest}, bindings);
            if (step.taken == Taken::done) {
                return build_.atom("done"_atom);
            }
            matched = matched && step.taken == Taken::match;
            rest = step.rest;
        }
        if (matched) {
            const std::array result{build_.atom("match"_atom), rest, bindings.term()};
            return build_.tuple(result);
        }
        const std::array result{build_.atom("nomatch"_atom), rest};
        return build_.tuple(result);
    }

  private:
    static void require(bool holds) {
        if (!holds) {
            throw NoMatch{};
        }
    }

    void literal(const Subject &subject, Bindings & /*bindings*/) {
        require(string::same(subject.term, element(subject.pattern, 2)));
    }

    void variable(const Subject &subject, Bindings &bindings) {
        const auto name = element(subject.pattern, 2);
        if (is_atom(name, "_")) {
            return;
        }
        if (const auto bound = bindings.find(name)) {
            require(string::same(*bound, subject.term));
            return;
        }
        bindings.add(name, subject.term);
    }

    void both(const Subject &subject, Bindings &bindings) {
        pattern({element(subject.pattern, 2), subject.term}, bindings);
        pattern({element(subject.pattern, 3), subject.term}, bindings);
    }

    void nil(const Subject &subject, Bindings & /*bindings*/) { require(subject.term.is_nil()); }

    void cons(const Subject &subject, Bindings &bindings) {
        require(subject.term.is_cons());
        pattern({element(subject.pattern, 2), subject.term.head_unchecked()}, bindings);
        pattern({element(subject.pattern, 3), subject.term.tail_unchecked()}, bindings);
    }

    void tuple(const Subject &subject, Bindings &bindings) {
        const auto elements = forms::elements(element(subject.pattern, 2));
        require(subject.term.is_tuple() && subject.term.tuple_size_unchecked() == elements.size());
        for (std::size_t at = 0; at < elements.size(); ++at) {
            pattern({elements[at], subject.term.tuple_element_unchecked(at)}, bindings);
        }
    }

    // A map pattern's keys are evaluated over the outer bindings; a key that fails or is missing does not match.
    void map(const Subject &subject, Bindings &bindings) {
        require(subject.term.is_map());
        for (const auto &field : forms::elements(element(subject.pattern, 2))) {
            if (!is_node(field, "map_field_exact", 4)) {
                lists::no_clause();
            }
            const auto key = outer_value(element(field, 2));
            const auto found = need(subject.term.map_find(key));
            require(found.has_value());
            pattern({element(field, 3), *found}, bindings);
        }
    }

    // A guard expression over the outer bindings; any failure is no match.
    Term outer_value(const Term &node) {
        try {
            return GuardEval(build_.context(), &outer_).value(node);
        } catch (...) {
            if (!evaluation_failure(std::current_exception())) {
                throw;
            }
        }
        throw NoMatch{};
    }

    // #Name{Field = Pattern} of a native record: #Module:Name or #Name, each field by its atom.
    void record(const Subject &subject, Bindings &bindings) {
        const auto view = detail::record_view(subject.term);
        require(view.has_value());
        auto &context = build_.context();
        const auto module = need(Term::from_word(view->definition->module, context));
        const auto record_name = need(Term::from_word(view->definition->name, context));
        const auto name = element(subject.pattern, 2);
        const auto qualified = name.is_tuple() && name.tuple_size_unchecked() == 2 &&
                               string::same(element(name, 0), module) && string::same(element(name, 1), record_name);
        require(qualified || string::same(name, record_name));
        for (const auto &field : forms::elements(element(subject.pattern, 3))) {
            if (!is_node(field, "record_field", 4) || !is_node(element(field, 2), "atom", 3)) {
                lists::no_clause();
            }
            const auto position = detail::record_position(*view->definition, element(element(field, 2), 2).word());
            require(position.has_value());
            pattern({element(field, 3), need(Term::from_word(view->values[*position], context))}, bindings);
        }
    }

    // "abc" ++ T, [$a] ++ T and [] ++ T as the conses they stand for; other operators fold to a constant.
    void operation(const Subject &subject, Bindings &bindings) {
        if (!is_atom(element(subject.pattern, 2), "++")) {
            constant(subject, bindings);
            return;
        }
        pattern({prefix(element(subject.pattern, 3), element(subject.pattern, 4)), subject.term}, bindings);
    }

    Term prefix(const Term &left, const Term &right) const {
        if (is_node(left, "nil", 2)) {
            return right;
        }
        if (is_node(left, "cons", 4) && forms::is_one_of(forms::tag_of(element(left, 2)), {"integer", "char"})) {
            const auto anno = element(left, 1);
            const auto tail = build_.node("op"_atom, anno, build_.atom("++"_atom), element(left, 3), right);
            return build_.node("cons"_atom, anno, element(left, 2), tail);
        }
        if (is_node(left, "string", 3)) {
            const auto anno = element(left, 1);
            const auto chars = forms::elements(element(left, 2));
            auto list = right;
            for (auto code = chars.rbegin(); code != chars.rend(); ++code) {
                list = build_.node("cons"_atom, anno, build_.node("integer"_atom, anno, *code), list);
            }
            return list;
        }
        throw Invalid{};
    }

    // An operator pattern is the constant it folds to.
    void constant(const Subject &subject, Bindings &bindings) {
        const auto folded = lint::partial_eval(build_.context(), subject.pattern);
        if (folded.word() == subject.pattern.word()) {
            throw Invalid{};
        }
        pattern({folded, subject.term}, bindings);
    }

    // match_bits/5: segments taken in order; anything but an illegal pattern failing is no match. Variables bound by
    // segments size the segments after them.
    void binary(const Subject &subject, Bindings &bindings) {
        require(subject.term.is_bitstring());
        const auto saved = outer_;
        try {
            auto rest = subject.term;
            for (const auto &field : forms::elements(element(subject.pattern, 2))) {
                rest = matched_segment({field, rest}, bindings);
            }
            require(rest.bit_size_unchecked() == 0);
        } catch (const Invalid &) {
            outer_ = saved;
            throw;
        } catch (...) {
            outer_ = saved;
            if (!any_failure_but_memory(std::current_exception())) {
                throw;
            }
            throw NoMatch{};
        }
        outer_ = saved;
    }

    // match_segment/5: the bits after the segment.
    Term matched_segment(const Piece &segment, Bindings &bindings) {
        const auto &field = segment.part;
        const auto &bits = segment.bits;
        if (!is_node(field, "bin_element", 5)) {
            lists::no_clause();
        }
        const auto value = element(field, 2);
        const auto size = element(field, 3);
        const auto types = element(field, 4);
        if (is_node(value, "string", 3) && is_atom(size, "default") && is_atom(types, "default")) {
            return string_prefix({element(value, 2), bits});
        }
        const auto spec = segment_spec(size, types);
        if (is_node(value, "string", 3)) {
            auto rest = bits;
            for (const auto &code : forms::elements(element(value, 2))) {
                rest = take(rest, spec, build_.node("integer"_atom, element(value, 1), code), bindings);
            }
            return rest;
        }
        return take(bits, spec, float_pattern(lint::partial_eval(build_.context(), value), spec[1]), bindings);
    }

    // A default string segment: the string's bytes at the front of the bits.
    Term string_prefix(const Piece &string) {
        const auto &chars = string.part;
        const auto &bits = string.bits;
        const auto codes = forms::elements(chars);
        const auto spec = byte_spec(static_cast<std::int64_t>(codes.size()));
        const auto taken = bit_take(build_.context(), bits, spec);
        require(taken.is_tuple());
        const auto expected =
            call_registered(build_.context(), {.module = "erlang", .function = "list_to_binary"}, std::array{chars});
        require(string::same(element(taken, 0), expected));
        return element(taken, 1);
    }

    std::array<Term, 2> byte_spec(std::int64_t count) const {
        const std::array fields{build_.atom("binary"_atom), build_.integer(8), build_.atom("unsigned"_atom),
                                build_.atom("big"_atom)};
        return {build_.integer(count), build_.tuple(fields)};
    }

    // segment_spec/5: the size (evaluated over the outer bindings) and the settled specification.
    std::array<Term, 2> segment_spec(const Term &size, const Term &types) {
        const std::array args{size, types};
        const auto type = bit_type(build_.context(), args);
        if (is_atom(size, "default")) {
            return {element(type, 0), element(type, 1)};
        }
        return {size_value(lint::partial_eval(build_.context(), element(type, 0))), element(type, 1)};
    }

    // size_value/2: a guard expression over bound variables, else an illegal pattern.
    Term size_value(const Term &expr) {
        auto &context = build_.context();
        if (!is_guard_expr(context, expr, {})) {
            throw Invalid{};
        }
        try {
            return GuardEval(context, &outer_).value(expr);
        } catch (const Unbound &) {
            throw Invalid{};
        }
    }

    // take_segment/7: the segment matched against `p`, a variable it binds also sizing later segments.
    Term take(const Term &bits, std::span<const Term, 2> spec, const Term &p, Bindings &bindings) {
        const auto taken = bit_take(build_.context(), bits, spec);
        require(taken.is_tuple());
        pattern({p, element(taken, 0)}, bindings);
        bind_outer(p, bindings);
        return element(taken, 1);
    }

    void bind_outer(const Term &p, const Bindings &bindings) {
        if (is_node(p, "var", 3) && !is_atom(element(p, 2), "_")) {
            outer_.add(element(p, 2), *bindings.find(element(p, 2)));
        }
    }

    // An integer pattern of a float segment matches as a float.
    Term float_pattern(const Term &p, const Term &spec) {
        if (!is_node(p, "integer", 3) || !is_atom(element(spec, 0), "float")) {
            return p;
        }
        const auto *to_float = detail::match_spec::guard_function("float", 1);
        detail::match_spec::Expression call;
        call.kind = detail::match_spec::Expression::Kind::call;
        call.function = to_float->function;
        call.operation = static_cast<decltype(call.operation)>(to_float->operation);
        const std::array words{element(p, 2).word()};
        const auto value = detail::match_spec::call_function(build_.context(), call, words);
        if (!value) {
            return p;
        }
        return build_.node("float"_atom, element(p, 1), need(Term::from_word(*value, build_.context())));
    }

    // gen_segment/5.
    Step generated_segment(const Piece &segment, Bindings &bindings) {
        const auto &field = segment.part;
        const auto &bits = segment.bits;
        if (!is_node(field, "bin_element", 5)) {
            lists::no_clause();
        }
        const auto value = element(field, 2);
        const auto size = element(field, 3);
        const auto types = element(field, 4);
        if (is_node(value, "string", 3) && is_atom(size, "default") && is_atom(types, "default")) {
            return generated_string_prefix({element(value, 2), bits});
        }
        const std::array args{size, types};
        const auto type = bit_type(build_.context(), args);
        const auto spec_term = element(type, 1);
        const auto evaluated = generated_size(is_atom(size, "default"), type);
        if (!evaluated) {
            return {.taken = Taken::done, .rest = bits};
        }
        const std::array spec{*evaluated, spec_term};
        if (is_node(value, "string", 3)) {
            return generated_string({value, bits}, spec, bindings);
        }
        return generated_take(bits, spec, float_pattern(lint::partial_eval(build_.context(), value), spec_term),
                              bindings);
    }

    Step generated_string_prefix(const Piece &string) {
        const auto &chars = string.part;
        const auto &bits = string.bits;
        const auto codes = forms::elements(chars);
        const auto spec = byte_spec(static_cast<std::int64_t>(codes.size()));
        const auto taken = bit_take(build_.context(), bits, spec);
        if (!taken.is_tuple()) {
            return {.taken = Taken::done, .rest = bits};
        }
        const auto expected = bytes_of(chars);
        const auto same = expected && string::same(element(taken, 0), *expected);
        return {.taken = same ? Taken::match : Taken::nomatch, .rest = element(taken, 1)};
    }

    std::optional<Term> bytes_of(const Term &chars) {
        try {
            return call_registered(build_.context(), {.module = "erlang", .function = "list_to_binary"},
                                   std::array{chars});
        } catch (...) {
            if (!evaluation_failure(std::current_exception())) {
                throw;
            }
        }
        return std::nullopt;
    }

    // gen_size/4: the settled default size when none is written, else the size expression's value over the outer
    // bindings; none when it fails. `type` is bit_type/2's {Size1, Spec}.
    std::optional<Term> generated_size(bool default_size, const Term &type) {
        const auto settled = element(type, 0);
        if (default_size) {
            return settled;
        }
        try {
            return GuardEval(build_.context(), &outer_).value(settled);
        } catch (...) {
            if (!evaluation_failure(std::current_exception())) {
                throw;
            }
        }
        return std::nullopt;
    }

    // gen_string/8: each character of a string node taken as an integer segment.
    Step generated_string(const Piece &string, std::span<const Term, 2> spec, Bindings &bindings) {
        auto rest = string.bits;
        const auto anno = element(string.part, 1);
        for (const auto &code : forms::elements(element(string.part, 2))) {
            const auto step = generated_take(rest, spec, build_.node("integer"_atom, anno, code), bindings);
            if (step.taken != Taken::match) {
                return step;
            }
            rest = step.rest;
        }
        return {.taken = Taken::match, .rest = rest};
    }

    // gen_take/7: a float segment that does not decode still consumes its bits when an integer of that size fits.
    Step generated_take(const Term &bits, std::span<const Term, 2> spec, const Term &p, Bindings &bindings) {
        auto &context = build_.context();
        const auto taken = bit_take(context, bits, spec);
        if (taken.is_tuple()) {
            auto attempt = bindings;
            try {
                pattern({p, element(taken, 0)}, attempt);
            } catch (...) {
                if (!any_failure_but_memory(std::current_exception())) {
                    throw;
                }
                return {.taken = Taken::nomatch, .rest = element(taken, 1)};
            }
            bindings = attempt;
            bind_outer(p, bindings);
            return {.taken = Taken::match, .rest = element(taken, 1)};
        }
        if (!is_atom(element(spec[1], 0), "float")) {
            return {.taken = Taken::done, .rest = bits};
        }
        const auto &float_spec = spec[1];
        const std::array integer_fields{build_.atom("integer"_atom), element(float_spec, 1), element(float_spec, 2),
                                        element(float_spec, 3)};
        const std::array integer_spec{spec[0], build_.tuple(integer_fields)};
        const auto integer = bit_take(context, bits, integer_spec);
        return integer.is_tuple() ? Step{.taken = Taken::nomatch, .rest = element(integer, 1)}
                                  : Step{.taken = Taken::done, .rest = bits};
    }

    forms::Builder build_;
    // The bindings map keys and sizes read: the outer ones, with the variables earlier segments bound.
    Bindings outer_;
};

// The roles of the arguments (typed.hpp's Named).
struct PatternRole;
struct TermRole;
struct BindingsRole;
struct OuterRole;
struct FieldsRole;
using PatternArg = Named<PatternRole>;
using TermArg = Named<TermRole>;
using BindingsArg = Named<BindingsRole>;
using OuterArg = Named<OuterRole>;
using FieldsArg = Named<FieldsRole>;

// clause_erl_eval:match(Pattern, Term, Bindings, OuterBindings): {match, Bindings1}, nomatch, or invalid for an
// illegal pattern.
Term match4(ProcessContext &context, const PatternArg &p, const TermArg &term, const BindingsArg &bs,
            const OuterArg &outer) {
    const forms::Builder build(context);
    Bindings bindings(build, bs.term);
    Matcher matcher(context, outer.term);
    try {
        matcher.pattern({p.term, term.term}, bindings);
    } catch (const NoMatch &) {
        return build.atom("nomatch"_atom);
    } catch (const Invalid &) {
        return build.atom("invalid"_atom);
    } catch (const Failed &) {
        return build.atom("nomatch"_atom);
    }
    const std::array result{build.atom("match"_atom), bindings.term()};
    return build.tuple(result);
}

// clause_erl_eval:bits_gen(Fields, Bits, Bindings, OuterBindings): a binary generator's pattern at the front of the
// bits; the specifier errors of erl_bits raise.
Term bits_gen4(ProcessContext &context, const FieldsArg &fields, const TermArg &bits, const BindingsArg &bs,
               const OuterArg &outer) {
    const forms::Builder build(context);
    Bindings bindings(build, bs.term);
    Matcher matcher(context, outer.term);
    return matcher.generate({fields.term, bits.term}, bindings);
}

Term partial_eval1(ProcessContext &context, const TermArg &expr) { return lint::partial_eval(context, expr.term); }

// is_constant_expr(Expr): whether Expr folds to a number.
Term is_constant_expr1(ProcessContext &context, const TermArg &expr) {
    const auto folded = lint::partial_eval(context, expr.term);
    const auto number = is_node(folded, "integer", 3) || is_node(folded, "float", 3);
    return string::Terms(context).boolean(number);
}

constexpr std::array EVAL_MATCH_BUILTINS{
    typed_entry<match4>("clause_erl_eval", "match"),
    typed_entry<bits_gen4>("clause_erl_eval", "bits_gen"),
    typed_entry<partial_eval1>("erl_eval", "partial_eval"),
    typed_entry<is_constant_expr1>("erl_eval", "is_constant_expr"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end::eval

namespace clause::runtime {
std::span<const BuiltinEntry> eval_match_builtins() noexcept { return builtins::front_end::eval::EVAL_MATCH_BUILTINS; }
} // namespace clause::runtime
