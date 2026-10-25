#include "../front_end/abstract.hpp"
#include "qlc_pt.hpp"
#include <algorithm>

// The run-time qlc transform's column analysis (plan 21 step LIB73): which columns of generator patterns filters
// compare with constants (lookup), with each other (join) and which filters a lookup or join makes redundant.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

namespace {
// {var, anno0(), Name}.
Term pattern_var_node(const Transformer &t, const Term &name) { return t.var(t.anno0(), name); }

bool is_const(const Term &value, const Imported &imported) {
    return ord_subtract(vars_of(value), imported.vars).empty();
}

const DerefTest &any_value() {
    static const DerefTest test = [](const Term &, const Term &) { return true; };
    return test;
}

// The values frames_to_columns/6 takes: every one (lu_skip/8's FailSelector), the constants (const_selector/1) or
// the others (no_const_selector/1).
enum class Select : std::uint8_t { all, constants, others };

// The operator test of a frames_to_columns/6 dereferencing (deref_values/4): any value (deref_pattern/1), matched
// values (deref_join/3 for '=:='), matched or integer-free values (deref_lookup/2 for '=:=') and compared or
// integer-free values (deref_lu_skip/2 for '==').
enum class DerefOp : std::uint8_t { any, matched, matched_or_free, compared_or_free };

// A frames_to_columns/6 dereferencing: the operator test and the imported variables it is made with.
struct DerefColumn {
    DerefOp op = DerefOp::any;
    Imported imported;
};

// The dereferenced values of a pattern column variable in a frame.
TermList deref_column(const Transformer &t, const DerefColumn &deref, const Term &var, const Term &frame) {
    const DerefTest test = [&](const Term &value, const Term &op) {
        switch (deref.op) {
        case DerefOp::any:
            return true;
        case DerefOp::matched:
            return is_atom(op, "=:=");
        case DerefOp::matched_or_free:
            return is_atom(op, "=:=") || t.free_of_integers(value, deref.imported);
        case DerefOp::compared_or_free:
            return is_atom(op, "==") || t.free_of_integers(value, deref.imported);
        }
        return false;
    };
    return t.deref_values(var, frame, test, deref.imported);
}

// pattern_sz/3.
std::optional<std::int64_t> pattern_sz(const Term &cons, bool exact) {
    std::int64_t size = 0;
    auto rest = cons;
    while (is_node(rest, "cons", 4)) {
        ++size;
        rest = element(rest, 3);
    }
    if (is_node(rest, "nil", 2) || !exact) {
        return size;
    }
    return std::nullopt;
}
} // namespace

// pattern_size/3: the size of the tuples a pattern variable is bound to, none for undefined.
namespace {
// lists:usort/1 of the sizes (undefined, sorting after the integers in OTP, is none here).
std::vector<std::optional<std::int64_t>> unique_sizes(std::vector<std::optional<std::int64_t>> sizes) {
    std::ranges::sort(sizes);
    const auto [first, last] = std::ranges::unique(sizes);
    sizes.erase(first, last);
    return sizes;
}

// A pattern variable whose tuple sizes are counted, exactly (a proper tuple) or not (a tuple tail allowed).
struct SizedVar {
    const Term &var;
    bool exact = false;
};

// The sizes of the {cons_tuple, Cs} a pattern variable is bound to in one frame.
void frame_sizes(const Transformer &t, const Term &frame, const SizedVar &sized,
                 std::vector<std::optional<std::int64_t>> &sizes) {
    for (const auto &value : t.deref_values(sized.var, frame, any_value(), {})) {
        if (value.is_tuple() && value.tuple_size_unchecked() == 2 && is_atom(element(value, 0), "cons_tuple")) {
            sizes.push_back(pattern_sz(element(value, 1), sized.exact));
        }
    }
}

std::optional<std::int64_t> pattern_size(const Transformer &t, const TermList &frames, const Term &pattern_var,
                                         bool exact) {
    std::vector<std::optional<std::int64_t>> sizes;
    for (const auto &frame : frames) {
        frame_sizes(t, frame, {.var = pattern_var, .exact = exact}, sizes);
    }
    sizes = unique_sizes(std::move(sizes));
    if (sizes.size() == 1 && sizes[0]) {
        return sizes[0];
    }
    if (sizes.empty() && !exact) {
        return 0;
    }
    return std::nullopt;
}

// One pattern variable of frames_to_columns/6: its qualifier number, variable, sizes per frame and column variables.
struct PatternColumns {
    Term number;
    Term var;
    std::vector<std::optional<std::int64_t>> sizes;
    TermList vars;
};

// The settings of frames_to_columns/6: the dereferencing, the values taken, the imported variables of the
// unifications and constants, and whether the operator is '==' (col_ignore/2).
struct ColumnWalk {
    DerefColumn deref;
    Select select = Select::all;
    const Imported &imported;
    bool compared = false;
};

// The selector's answer for a column's single value: the value, or none.
std::optional<Term> selected_value(const ColumnWalk &walk, const Term &value) {
    if (walk.select == Select::all) {
        return value;
    }
    const auto constant = is_const(value, walk.imported);
    return constant == (walk.select == Select::constants) ? std::optional{value} : std::nullopt;
}

// frames2cols/7's relation of one frame: {{QualifierNumber, Column}, Value} for each column with one selected value.
TermList frame_relation(Transformer &t, const Term &frame, std::int64_t size, const PatternColumns &pattern,
                        const ColumnWalk &walk) {
    TermList relation;
    for (std::int64_t column = 1; column <= size; ++column) {
        const auto values = deref_column(t, walk.deref, pattern.vars[static_cast<std::size_t>(column - 1)], frame);
        const auto selected = values.size() == 1 ? selected_value(walk, values[0]) : std::nullopt;
        if (selected) {
            const auto key = t.pair(pattern.number, t.integer(column));
            relation.push_back(t.pair(key, cons_to_tuple(t.build(), *selected)));
        }
    }
    return usorted(std::move(relation));
}

// The columns every relation has (sofs:intersection of their projections).
TermList shared_columns(const std::vector<TermList> &relations) {
    TermList shared;
    for (std::size_t at = 0; at < relations.size(); ++at) {
        TermList keys;
        for (const auto &entry : relations[at]) {
            keys.push_back(element(entry, 0));
        }
        keys = usorted(std::move(keys));
        shared = at == 0 ? keys : ord_intersection(shared, keys);
    }
    return shared;
}

// col_ignore/2 for '==': some of the values compare equal.
bool compares_equal_values(const Transformer &t, const TermList &values) {
    TermList constants;
    for (const auto &value : values) {
        const auto constant = t.normalise(value);
        if (!constant) {
            bad_argument();
        }
        constants.push_back(*constant);
    }
    return usorted(std::move(constants)).size() != values.size();
}

// frames2cols/7 for one pattern variable.
TermList frames2cols(Transformer &t, const TermList &frames, const PatternColumns &pattern, const ColumnWalk &walk) {
    std::vector<TermList> relations;
    for (std::size_t at = 0; at < frames.size(); ++at) {
        if (!pattern.sizes[at]) {
            lists::no_clause();
        }
        relations.push_back(frame_relation(t, frames[at], *pattern.sizes[at], pattern, walk));
    }
    const auto shared = shared_columns(relations);
    TermList all;
    for (const auto &relation : relations) {
        all.insert(all.end(), relation.begin(), relation.end());
    }
    TermList out;
    for (const auto &entry : family_of(t.build(), all)) {
        const auto kept = std::ranges::binary_search(shared, element(entry, 0), less) &&
                          !(walk.compared && compares_equal_values(t, elements(element(entry, 1))));
        if (kept) {
            out.push_back(entry);
        }
    }
    return out;
}

// frames_to_columns/6's SizesVarsL entry of one pattern variable: its sizes per frame and fresh column variables.
PatternColumns pattern_columns(Transformer &t, const TermList &frames, const Term &entry) {
    PatternColumns pattern{.number = t.integer(qid_no(element(entry, 0))),
                           .var = pattern_var_node(t, element(entry, 1)),
                           .sizes = {},
                           .vars = {}};
    std::int64_t largest = 0;
    std::size_t undefined = 0;
    for (const auto &frame : frames) {
        pattern.sizes.push_back(pattern_size(t, {frame}, pattern.var, false));
        largest = std::max(largest, pattern.sizes.back().value_or(0));
        undefined += pattern.sizes.back() ? 0 : 1;
    }
    // lists:max([0 | Sizes -- [undefined]]) is undefined when a second size is: pat_vars/1 then fails.
    if (undefined > 1) {
        lists::no_clause();
    }
    for (std::int64_t at = 0; at < largest; ++at) {
        pattern.vars.push_back(t.unique_var());
    }
    return pattern;
}

// The frames with the pattern variable unified with a tuple of its size ({V1, ..., Vn | Tail}), when it has one.
TermList sized_frames(Transformer &t, const TermList &frames, const PatternColumns &pattern, const Imported &imported) {
    const BindTest bindable = [&](const Term &, const Term &value) { return t.is_bindable(value); };
    TermList next;
    for (std::size_t at = 0; at < frames.size(); ++at) {
        const auto size = pattern.sizes[at];
        Term tuple = t.unique_var();
        if (size && *size > 0) {
            const auto prefix = std::span(pattern.vars).first(static_cast<std::size_t>(*size));
            tuple = t.pair(t.atom("cons_tuple"_atom), list_to_cons(t.build(), prefix, tuple));
        }
        next.push_back(t.unify(t.atom("=:="_atom), tuple, pattern.var, frames[at], bindable, imported));
    }
    return next;
}

// frames_to_columns/6: per pattern variable, the columns bound in every frame with their values.
std::vector<TermList> frames_to_columns(Transformer &t, const TermList &frames, const ColumnWalk &walk,
                                        const TermList &pattern_vars) {
    if (frames.empty()) {
        return {};
    }
    std::vector<PatternColumns> patterns;
    for (const auto &entry : pattern_vars) {
        patterns.push_back(pattern_columns(t, frames, entry));
    }
    auto current = frames;
    for (const auto &pattern : patterns) {
        current = sized_frames(t, current, pattern, walk.imported);
    }
    std::vector<TermList> out;
    out.reserve(patterns.size());
    for (const auto &pattern : patterns) {
        out.push_back(frames2cols(t, current, pattern, walk));
    }
    return out;
}

TermList flat_columns(const std::vector<TermList> &columns) {
    TermList out;
    for (const auto &group : columns) {
        out.insert(out.end(), group.begin(), group.end());
    }
    return out;
}

// skip_tag/2: all when every filter is skipped.
Term skip_tag(const Transformer &t, const Term &filters, std::size_t filter_count) {
    const auto all = elements(filters).size() == filter_count;
    return t.pair(t.atom(all ? AtomName("all"_atom) : AtomName("some"_atom)), filters);
}
} // namespace

QualData Transformer::qual_data(const TermList &qualifiers) const {
    QualData out;
    for (const auto &qualifier : qualifiers) {
        if (!(qualifier.is_tuple() && qualifier.tuple_size_unchecked() == 4)) {
            continue;
        }
        const auto data = element(qualifier, 3);
        if (!data.is_tuple() || data.tuple_size_unchecked() == 0) {
            continue;
        }
        const auto entry = pair(element(qualifier, 0), data);
        if (is_atom(element(data, 0), "fil")) {
            out.filters.push_back(entry);
        } else if (is_atom(element(data, 0), "gen")) {
            out.generators.push_back(entry);
        }
    }
    return out;
}

Term Transformer::filters_as_one(const TermList &filters) const {
    if (filters.empty()) {
        return atom_node(anno0(), atom("true"_atom));
    }
    // OTP folds the reversed list from the right: Fn-1 andalso (... (F1 andalso Fn)).
    auto out = element(element(filters.back(), 1), 1);
    for (std::size_t at = 0; at + 1 < filters.size(); ++at) {
        out = build_.node("op"_atom, anno0(), atom("andalso"_atom), element(element(filters[at], 1), 1), out);
    }
    return out;
}

namespace {
using Edges = front_end::lint::TermMap<TermList>;
// Intro as a map: the qualifiers introducing each variable.
using Introducers = front_end::lint::TermMap<TermList>;

// The qualifiers other than the filter that introduce the variables the filter uses.
TermList filter_edges(const Introducers &introducers, const Term &filter) {
    TermList out;
    for (const auto &name : vars_of(element(element(filter, 1), 1))) {
        const auto found = introducers.find(name);
        const auto ids = found == introducers.end() ? TermList{} : found->second;
        std::ranges::copy_if(ids, std::back_inserter(out),
                             [&](const Term &id) { return !equal(id, element(filter, 0)); });
    }
    return out;
}

// The strict relative product of Used ({Filter, Var}) and Intro ({Var, Qualifier}): the graph's edges.
Edges dependency_edges(const TermList &filters, const Introducers &introducers) {
    Edges edges;
    for (const auto &filter : filters) {
        auto to = filter_edges(introducers, filter);
        if (!to.empty()) {
            edges[element(filter, 0)] = std::move(to);
        }
    }
    return edges;
}

// digraph_utils:reachable_neighbours([From], G): the vertices reached by one edge or more.
TermList reachable(const Edges &edges, const Term &from) {
    front_end::lint::TermSet reached;
    TermList pending{from};
    while (!pending.empty()) {
        const auto found = edges.find(pending.back());
        pending.pop_back();
        if (found == edges.end()) {
            continue;
        }
        for (const auto &to : found->second) {
            if (reached.insert(to).second) {
                pending.push_back(to);
            }
        }
    }
    return {reached.begin(), reached.end()};
}
} // namespace

TermList Transformer::qualifier_dependencies(const TermList &qualifiers, const IntroVars &intro) const {
    Introducers introducers;
    for (const auto &entry : intro.entries) {
        for (const auto &name : elements(element(entry, 1))) {
            introducers[name].push_back(element(entry, 0));
        }
    }
    const auto filters = qual_data(qualifiers).filters;
    const auto edges = dependency_edges(filters, introducers);
    TermList out;
    for (const auto &filter : filters) {
        out.push_back(pair(element(filter, 0), list(reachable(edges, element(filter, 0)))));
    }
    return usorted(std::move(out));
}

namespace {
// sel_gf/6's generator numbers: Gens since the last filter left out, Gens1 also since the last filter selected that
// depends on several generators.
class GeneratorScopes final {
  public:
    void add(std::int64_t number) {
        gens_.insert(gens_.begin(), number);
        gens1_.insert(gens1_.begin(), number);
    }

    void clear() {
        gens_.clear();
        gens1_.clear();
    }

    // Whether a guard filter depending on `ids` is selected: its generators are in scope.
    bool select(const TermList &ids) {
        const auto one = ids.size() <= 1;
        const auto &numbers = one ? gens1_ : gens_;
        const auto covered =
            std::ranges::all_of(ids, [&](const Term &id) { return std::ranges::contains(numbers, qid_no(id)); });
        if (!covered) {
            clear();
        } else if (!one) {
            gens1_.clear();
        }
        return covered;
    }

  private:
    std::vector<std::int64_t> gens_;
    std::vector<std::int64_t> gens1_;
};
} // namespace

TermList Transformer::filter_list(const TermList &filters, const Analysis &analysis) const {
    TermList out;
    GeneratorScopes scopes;
    std::int64_t number = 1;
    for (std::size_t at = 0; at < filters.size(); ++number) {
        const auto &filter = filters[at];
        if (qid_no(element(filter, 0)) != number) {
            scopes.add(number);
            continue;
        }
        ++at;
        if (!is_guard_test(element(element(filter, 1), 1))) {
            scopes.clear();
            continue;
        }
        const auto found = keyfind(element(filter, 0), 0, analysis.dependencies);
        if (!found) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
        }
        if (scopes.select(elements(element(*found, 1)))) {
            out.push_back(filter);
        }
    }
    return out;
}

Transformer::FilterInfo Transformer::filter_info(const TermList &filters, const Analysis &analysis) {
    const auto selected = filter_list(filters, analysis);
    FilterInfo info;
    info.filter = anonymous_numbered(build_, with_anno(build_, filters_as_one(selected), anno0()), info.anon);
    info.imported.vars = ord_subtract(vars_of(info.filter), usorted(analysis.all_ivs));
    return info;
}

std::pair<Term, TermList> Transformer::pattern_frame(const TermList &generators, const BindTest &bind,
                                                     std::int64_t anon) {
    auto frame = nil();
    TermList pattern_vars;
    for (const auto &generator : generators) {
        Term pattern_var = nil();
        frame = pattern(element(element(generator, 1), 1), anon, frame, bind, pattern_var);
        pattern_vars.insert(pattern_vars.begin(), pair(element(generator, 0), pattern_var));
    }
    return {frame, pattern_vars};
}

namespace {
TermList frames_of(const Term &frame) { return is_atom(frame, "failed") ? TermList{} : TermList{frame}; }

Term simple_template(Transformer &t, const Term &expr);

// simple_template/1 of the tuples and conses it walks into; none for other nodes.
std::optional<Term> simple_parts(Transformer &t, const Term &expr) {
    if (const auto call = element_call(expr)) {
        const auto index = elements(element(expr, 3)).front();
        const auto inner = simple_template(t, call->second);
        return t.build().node("call"_atom, element(expr, 1), element(expr, 2), t.list(std::array{index, inner}));
    }
    if (is_node(expr, "tuple", 3)) {
        TermList items;
        for (const auto &item : elements(element(expr, 2))) {
            items.push_back(simple_template(t, item));
        }
        return t.build().node("tuple"_atom, element(expr, 1), t.list(items));
    }
    if (is_node(expr, "cons", 4)) {
        const auto head = simple_template(t, element(expr, 2));
        return t.build().node("cons"_atom, element(expr, 1), head, simple_template(t, element(expr, 3)));
    }
    return std::nullopt;
}

// simple_template/1: the template as a pattern, any part that is not a literal a fresh variable.
Term simple_template(Transformer &t, const Term &expr) {
    if (is_node(expr, "var", 3)) {
        return expr;
    }
    if (auto parts = simple_parts(t, expr)) {
        return *parts;
    }
    try {
        static_cast<void>(front_end::abstract::normalise(t.build().context(), expr));
        return expr;
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return t.unique_var();
    }
}

Term template_as_pattern(Transformer &t, const Term &expr) {
    const auto qid = t.tuple({t.atom("qid"_atom), t.atom("template"_atom), t.integer(0)});
    const auto generator =
        t.tuple({t.atom("gen"_atom), simple_template(t, expr), t.build().node("nil"_atom, t.anno0())});
    return t.tuple({qid, t.atom("foo"_atom), t.atom("foo"_atom), generator});
}
} // namespace

// --- constants_and_sizes/5 ---

namespace {
// What a constants/9 or lu_skip/8 run needs from constants_and_sizes/5.
struct LookupScope {
    const TermList &pattern_vars;
    const TermList &pattern_constants;
    const Term &pattern_frame;
    const TermList &filters;
    const Analysis &analysis;
    const Imported &imported;
    const Term &filter;
};

// lu_skip/8's test of one frame: the lookup of the column's constants makes the filter redundant.
bool skips_frame(Transformer &t, const LookupScope &scope, const Term &frame, const Term &column_entry,
                 const TermList &column_constants) {
    const BindTest bindable = [&](const Term &, const Term &value) { return t.is_bindable(value); };
    const auto column = element(column_entry, 0);
    const auto number = *string::small_of(element(column, 1));
    const auto pattern_var = element(column_entry, 2);
    const auto [var_i, frame_i] = t.unify_column(frame, number, pattern_var, bindable, scope.imported);
    const auto var_values = t.deref(var_i, frame_i, any_value(), scope.imported);
    const auto [nv, f1] = t.unify_column(scope.pattern_frame, number, pattern_var, bindable, scope.imported);
    const auto f2 =
        t.unify_var_bindings(var_values, {.value = nv, .op = t.atom("=:="_atom)}, f1, bindable, scope.imported, false);
    TermList looked_up;
    if (const auto found = keyfind(column, 0, column_constants)) {
        looked_up = elements(element(*found, 1));
    }
    return var_values.size() <= 1 && minus(elements(element(column_entry, 1)), {looked_up}).empty() &&
           t.bindings_is_subset(Transformer::frame_binds(frame), f2, scope.imported);
}

// What lu_skip/8 tests each filter against.
struct SkipInputs {
    const TermList &column_constants;
    const TermList &pattern_columns;
    bool compared = false;
};

// lu_skip/8's ColFil entries of one filter: {Column, FilterNumber} for each column a lookup makes it redundant for.
TermList skipped_columns(Transformer &t, const LookupScope &scope, const SkipInputs &in, const Term &filter) {
    const BindTest bindable = [&](const Term &, const Term &value) { return t.is_bindable(value); };
    const auto safe_frames = t.filter(with_anno(t.build(), element(element(filter, 1), 1), t.anno0()),
                                      frames_of(scope.pattern_frame), bindable, scope.imported, true);
    TermList out;
    const auto pattern_vars = safe_frames.empty() ? TermList{} : scope.pattern_vars;
    for (const auto &pattern_var : pattern_vars) {
        const ColumnWalk walk{
            .deref = {.op = in.compared ? DerefOp::compared_or_free : DerefOp::any, .imported = scope.imported},
            .select = Select::constants,
            .imported = scope.imported,
            .compared = in.compared};
        const auto columns = frames_to_columns(t, safe_frames, walk, {pattern_var}).front();
        const auto differing = columns.empty() ? TermList{} : minus(columns, {in.pattern_columns});
        if (differing.size() != 1) {
            continue;
        }
        const auto entry = t.tuple({element(differing[0], 0), element(differing[0], 1), element(pattern_var, 1)});
        const auto all = std::ranges::all_of(
            safe_frames, [&](const Term &frame) { return skips_frame(t, scope, frame, entry, in.column_constants); });
        if (all) {
            out.push_back(t.pair(element(differing[0], 0), t.integer(qid_no(element(filter, 0)))));
        }
    }
    return out;
}

// lu_skip/8: each column's constants with the filters a lookup of them skips.
TermList lu_skip(Transformer &t, const LookupScope &scope, const TermList &column_constants, bool compared) {
    const ColumnWalk fail_walk{.deref = {.op = DerefOp::any, .imported = scope.imported},
                               .select = Select::all,
                               .imported = scope.imported,
                               .compared = compared};
    const auto pattern_columns =
        flat_columns(frames_to_columns(t, frames_of(scope.pattern_frame), fail_walk, scope.pattern_vars));
    const SkipInputs in{.column_constants = column_constants, .pattern_columns = pattern_columns, .compared = compared};
    TermList column_filters;
    for (const auto &filter : t.filter_list(scope.filters, scope.analysis)) {
        const auto skipped = skipped_columns(t, scope, in, filter);
        column_filters.insert(column_filters.end(), skipped.begin(), skipped.end());
    }
    const auto families = family_of(t.build(), column_filters);
    TermList out;
    for (const auto &entry : column_constants) {
        const auto found = keyfind(element(entry, 0), 0, families);
        const auto tag =
            found ? skip_tag(t, element(*found, 1), scope.filters.size()) : t.pair(t.atom("some"_atom), t.nil());
        out.push_back(t.tuple({element(entry, 0), element(entry, 1), tag}));
    }
    return out;
}

// constants/9: the lookup constants of each column for an operator, and the extra constants of joins.
std::pair<TermList, TermList> constants(Transformer &t, const LookupScope &scope, bool compared) {
    const BindTest bindable = [&](const Term &, const Term &value) { return t.is_bindable(value); };
    const auto pattern_frames = frames_of(scope.pattern_frame);
    const auto frames = t.filter(scope.filter, pattern_frames, bindable, scope.imported, false);
    const ColumnWalk walk{
        .deref = {.op = compared ? DerefOp::any : DerefOp::matched_or_free, .imported = scope.imported},
        .select = Select::constants,
        .imported = scope.imported,
        .compared = compared};
    const auto column_constants = flat_columns(frames_to_columns(t, frames, walk, scope.pattern_vars));
    TermList extra;
    for (const auto &entry : minus(column_constants, {scope.pattern_constants})) {
        const auto column = element(entry, 0);
        if (string::small_of(element(column, 0)) == 0) {
            continue;
        }
        for (const auto &value : elements(element(entry, 1))) {
            extra.push_back(t.pair(column, value));
        }
    }
    return {lu_skip(t, scope, column_constants, compared), extra};
}
} // namespace

Transformer::Constants Transformer::constants_and_sizes(const TermList &qualifiers0, const Term &expr,
                                                        const Analysis &analysis) {
    TermList qualifiers{template_as_pattern(*this, expr)};
    qualifiers.insert(qualifiers.end(), qualifiers0.begin(), qualifiers0.end());
    const auto data = qual_data(qualifiers);
    const auto info = filter_info(data.filters, analysis);
    const BindTest bindable = [&](const Term &, const Term &value) { return is_bindable(value); };
    const auto [frame, pattern_vars] = pattern_frame(data.generators, bindable, info.anon);
    const auto pattern_frames = frames_of(frame);
    const auto size_frames = filter(info.filter, pattern_frames, bindable, info.imported, false);
    TermList sizes;
    for (const auto &entry : pattern_vars) {
        const auto size = pattern_size(*this, size_frames, pattern_var_node(*this, element(entry, 1)), true);
        if (size) {
            sizes.push_back(pair(integer(qid_no(element(entry, 0))), integer(*size)));
        }
    }
    const ColumnWalk walk{.deref = {.op = DerefOp::any, .imported = info.imported},
                          .select = Select::constants,
                          .imported = info.imported,
                          .compared = false};
    const auto pattern_constants = flat_columns(frames_to_columns(*this, pattern_frames, walk, pattern_vars));
    const LookupScope scope{.pattern_vars = pattern_vars,
                            .pattern_constants = pattern_constants,
                            .pattern_frame = frame,
                            .filters = data.filters,
                            .analysis = analysis,
                            .imported = info.imported,
                            .filter = info.filter};
    const auto [eq_constants, eq_extra] = constants(*this, scope, false);
    static_cast<void>(eq_extra);
    const auto [equal_constants, equal_extra] = constants(*this, scope, true);
    TermList extra;
    for (const auto &entry : equal_extra) {
        extra.push_back(pair(element(entry, 0), pair(element(entry, 1), atom("=="_atom))));
    }
    TermList per_generator;
    for (const auto &entry : family_of(build_, extra)) {
        const auto column = element(entry, 0);
        per_generator.push_back(pair(element(column, 0), pair(element(column, 1), element(entry, 1))));
    }
    return {.eq_column_constants = list(eq_constants),
            .equal_column_constants = list(equal_constants),
            .extra_constants = list(family_of(build_, per_generator)),
            .size_info = list(sizes)};
}

// --- join columns ---

namespace {
// join_classes/1: the columns with the same variable, in classes of more than one column.
TermList join_classes(const Transformer &t, const std::vector<TermList> &columns) {
    const auto relation = usorted(flat_columns(columns));
    front_end::lint::TermMap<TermList> classes;
    for (const auto &entry : relation) {
        classes[element(entry, 1)].push_back(entry);
    }
    TermList partitions;
    for (auto &[values, entries] : classes) {
        static_cast<void>(values);
        partitions.push_back(t.list(entries));
    }
    TermList out;
    for (const auto &partition : sorted(std::move(partitions))) {
        const auto entries = elements(partition);
        if (entries.size() > 1) {
            TermList cols;
            for (const auto &entry : entries) {
                cols.push_back(element(entry, 0));
            }
            out.push_back(t.list(cols));
        }
    }
    return out;
}

// The pattern variables ({GId, PV}) of the generator numbered `number`.
TermList pattern_vars_numbered(const TermList &pattern_vars, const Term &number) {
    TermList out;
    for (const auto &entry : pattern_vars) {
        if (string::small_of(number) == qid_no(element(entry, 0))) {
            out.push_back(entry);
        }
    }
    return out;
}

// What join_skip/8 needs of join_info/5.
struct JoinScope {
    const TermList &filters;
    const Term &pattern_frame;
    const TermList &pattern_vars;
    const Analysis &analysis;
    const Imported &imported;
    const Term &join_op;
};

// join_skip/8's test of one filter: running it adds nothing to the join of the two columns.
bool join_skips(Transformer &t, const JoinScope &scope, const std::array<Term, 4> &sides, const Term &filter) {
    const BindTest bindable = [&](const Term &, const Term &value) { return t.is_bindable(value); };
    const auto [v1, jf1] = t.unify_column(scope.pattern_frame, *string::small_of(sides[1]), element(sides[0], 1),
                                          bindable, scope.imported);
    const auto [v2, jf2] =
        t.unify_column(jf1, *string::small_of(sides[3]), element(sides[2], 1), bindable, scope.imported);
    const auto jf = t.unify(scope.join_op, v1, v2, jf2, bindable, scope.imported);
    const auto safe_frames = t.filter(with_anno(t.build(), element(element(filter, 1), 1), t.anno0()),
                                      frames_of(scope.pattern_frame), bindable, scope.imported, true);
    const Imported join_imported{.all = false, .vars = vars_of(t.list(std::array{t.list(safe_frames), jf}))};
    const auto all = std::ranges::all_of(safe_frames, [&](const Term &frame) {
        return t.bindings_is_subset(Transformer::frame_binds(frame), jf, join_imported);
    });
    return all && !safe_frames.empty();
}

// join_skip/8's filters for one pair of pattern variables of a join class ({GId, PV} each, and their columns).
TermList pair_skips(Transformer &t, const JoinScope &scope, const Term &join_class, const std::array<Term, 4> &sides) {
    TermList out;
    const auto generators = t.list(sorted({element(sides[0], 0), element(sides[2], 0)}));
    for (const auto &filter : t.filter_list(scope.filters, scope.analysis)) {
        const auto found = keyfind(element(filter, 0), 0, scope.analysis.dependencies);
        if (found && same(element(*found, 1), generators) && join_skips(t, scope, sides, filter)) {
            out.push_back(t.pair(join_class, t.integer(qid_no(element(filter, 0)))));
        }
    }
    return out;
}

// join_skip/8's filters for one join class of two columns.
TermList class_skips(Transformer &t, const JoinScope &scope, const Term &join_class) {
    const auto columns = elements(join_class);
    TermList out;
    if (columns.size() != 2) {
        return out;
    }
    for (const auto &first : pattern_vars_numbered(scope.pattern_vars, element(columns[0], 0))) {
        for (const auto &second : pattern_vars_numbered(scope.pattern_vars, element(columns[1], 0))) {
            const std::array sides{first, element(columns[0], 1), second, element(columns[1], 1)};
            const auto skipped = pair_skips(t, scope, join_class, sides);
            out.insert(out.end(), skipped.begin(), skipped.end());
        }
    }
    return out;
}

// join_skip/8: per join class of two columns, the filters the join makes redundant.
TermList join_skip(Transformer &t, const JoinScope &scope, const TermList &classes) {
    TermList column_filters;
    for (const auto &join_class : classes) {
        const auto skipped = class_skips(t, scope, join_class);
        column_filters.insert(column_filters.end(), skipped.begin(), skipped.end());
    }
    return family_of(t.build(), column_filters);
}

// join_info/5: the join classes of the columns and the filters each class makes redundant.
std::pair<TermList, TermList> join_info(Transformer &t, const TermList &qualifiers, const Analysis &analysis,
                                        bool compared) {
    const auto data = t.qual_data(qualifiers);
    const auto info = t.filter_info(data.filters, analysis);
    const BindTest bind = [&](const Term &, const Term &value) {
        return !is_const(value, info.imported) && t.is_bindable(value);
    };
    const auto [pattern_frame, pattern_vars] = t.pattern_frame(data.generators, bind, info.anon);
    const auto frames = t.filter(info.filter, frames_of(pattern_frame), bind, info.imported, false);
    // deref_join/3: '==' compares through any binding with no imported variables; '=:=' keeps matched values with
    // every variable imported.
    const auto deref = compared ? DerefColumn{.op = DerefOp::any, .imported = {}}
                                : DerefColumn{.op = DerefOp::matched, .imported = {.all = true, .vars = {}}};
    const ColumnWalk walk{.deref = deref, .select = Select::others, .imported = info.imported, .compared = false};
    const auto classes = join_classes(t, frames_to_columns(t, frames, walk, pattern_vars));
    const auto join_op = t.atom(compared ? AtomName("=="_atom) : AtomName("=:="_atom));
    const JoinScope scope{.filters = data.filters,
                          .pattern_frame = pattern_frame,
                          .pattern_vars = pattern_vars,
                          .analysis = analysis,
                          .imported = info.imported,
                          .join_op = join_op};
    return {classes, join_skip(t, scope, classes)};
}

// join_gens/3: the joins of two generators and those of more, each selection with the filters it skips.
std::pair<TermList, TermList> join_gens(Transformer &t, const std::pair<TermList, TermList> &info,
                                        const TermList &qualifiers) {
    const auto filters = t.qual_data(qualifiers).filters;
    std::pair<TermList, TermList> out;
    for (const auto &join_class : info.first) {
        const auto family = family_of(t.build(), elements(join_class));
        if (family.size() < 2) {
            continue;
        }
        auto &into = family.size() == 2 ? out.first : out.second;
        for (const auto &selection : all_selections(t.build(), family)) {
            const auto found = keyfind(selection, 0, info.second);
            const auto skipped = found ? element(*found, 1) : t.nil();
            into.push_back(t.pair(selection, skip_tag(t, skipped, filters.size())));
        }
    }
    return out;
}

// The template's columns ({0, C}) of a join class, usorted.
TermList template_positions(const TermList &columns) {
    TermList positions;
    for (const auto &column : columns) {
        if (string::small_of(element(column, 0)) == 0) {
            positions.push_back(element(column, 1));
        }
    }
    return usorted(std::move(positions));
}

// template_cols/1.
Term template_cols(const Transformer &t, const TermList &classes) {
    TermList out;
    for (const auto &join_class : classes) {
        const auto columns = elements(join_class);
        const auto in_template = template_positions(columns);
        if (in_template.empty()) {
            continue;
        }
        const auto positions = t.list(in_template);
        for (const auto &column : columns) {
            if (string::small_of(element(column, 0)) != 0) {
                out.push_back(t.pair(column, positions));
            }
        }
    }
    return t.list(sorted(std::move(out)));
}
} // namespace

std::pair<Term, Term> Transformer::join_kind(const TermList &qualifiers, const Term &line, const Analysis &analysis) {
    const auto [equal2, equal_many] = join_gens(*this, join_info(*this, qualifiers, analysis, true), qualifiers);
    const auto [match2, match_many] = join_gens(*this, join_info(*this, qualifiers, analysis, false), qualifiers);
    TermList tables;
    for (const auto &join : equal2) {
        for (const auto &column : elements(element(join, 0))) {
            tables.push_back(element(column, 0));
        }
    }
    const auto warning = [&](AtomName tag) {
        const auto message = tuple({line, atom("qlc"_atom), atom(tag)});
        return std::pair{nil(), list(std::array{pair(file_, list(std::array{message}))})};
    };
    if (!equal_many.empty() || !match_many.empty()) {
        return warning("too_complex_join"_atom);
    }
    if (equal2.empty() && match2.empty()) {
        return {nil(), nil()};
    }
    if (usorted(std::move(tables)).size() > 2) {
        return warning("too_many_joins"_atom);
    }
    if (same(list(equal2), list(match2))) {
        return {list(equal2), nil()};
    }
    return {pair(list(equal2), list(match2)), nil()};
}

TermList Transformer::template_columns(const TermList &qualifiers0, const Term &expr0, const Analysis &analysis) {
    const auto expr = expand_expr_records(pre_expanded(build_, expr0));
    TermList qualifiers{template_as_pattern(*this, expr)};
    qualifiers.insert(qualifiers.end(), qualifiers0.begin(), qualifiers0.end());
    const auto equal_columns = join_info(*this, qualifiers, analysis, true).first;
    const auto match_columns = join_info(*this, qualifiers, analysis, false).first;
    const auto equal = template_cols(*this, equal_columns);
    const auto match = template_cols(*this, match_columns);
    if (same(match, equal)) {
        return {pair(var(anno0(), atom("_"_atom)), match)};
    }
    return {pair(atom_node(anno0(), atom("=="_atom)), equal), pair(atom_node(anno0(), atom("=:="_atom)), match)};
}
} // namespace clause::runtime::builtins::qlc_pt
