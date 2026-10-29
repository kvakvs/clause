#include "front_end.hpp"
#include "ms_transform.hpp"
#include <algorithm>
#include <array>
#include <string>

// ms_transform's clauses and head patterns in C++ (C++ ms_transform): clause_list/5, ms_clause/4, check_head/3,
// transform_head/2 and th/2 of library/stdlib/ms_transform.erl, with the record helpers both parts share.
namespace clause::runtime::builtins::front_end::ms {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
// function_clause, as an Erlang clause head without a match raises it.
[[noreturn]] void no_clause() { throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause}; }

// The {var, _, _} of a pattern.
bool is_var(const Term &term) { return is_node(term, "var", 3); }

// check_head/3's shapes: one variable, an ets tuple or record, a dbg list, either matched with a variable.
bool head_shape(const Term &pattern, Dialect dialect) {
    if (is_var(pattern)) {
        return true;
    }
    if (is_node(pattern, "match", 4)) {
        if (is_var(element(pattern, 2))) {
            return head_shape(element(pattern, 3), dialect);
        }
        return is_var(element(pattern, 3)) && head_shape(element(pattern, 2), dialect);
    }
    if (dialect == Dialect::ets) {
        return is_node(pattern, "tuple", 3) || is_node(pattern, "record", 4);
    }
    return is_node(pattern, "cons", 4) || is_node(pattern, "nil", 2);
}

// The only element of a one-element list.
std::optional<Term> only(const Term &list) {
    if (!list.is_cons() || !list.tail_unchecked().is_nil()) {
        return std::nullopt;
    }
    return list.head_unchecked();
}

// list_to_integer/1 accepts the text: an optional sign and at least one decimal digit.
bool integer_text(std::string_view text) {
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        text.remove_prefix(1);
    }
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

// record_field/2: a field's name, position and default ('undefined' when it has none); typed fields as untyped.
Field record_field(const Builder &build, Term field, std::int64_t position) {
    while (is_node(field, "typed_record_field", 3)) {
        field = element(field, 1);
    }
    const auto size = field.is_tuple() ? field.tuple_size_unchecked() : 0;
    if (forms::tag_of(field) != "record_field" || (size != 3 && size != 4) || !is_node(element(field, 2), "atom", 3)) {
        no_clause();
    }
    const auto name = element(field, 2);
    const auto value = size == 4 ? element(field, 3) : build.atom_node(element(name, 1), "undefined"_atom);
    return {.name = element(name, 2), .position = position, .default_value = value};
}

// The text of an atom; badarg for anything else (atom_to_list/1).
std::string_view spelling(const Term &atom) {
    if (!atom.is_atom()) {
        bad_argument();
    }
    return atom.atom_spelling_unchecked();
}
} // namespace

void Translator::fail(const Term &anno, const Term &reason) const { throw MsError{.anno = anno, .reason = reason}; }

void Translator::fail_part(const Term &anno, std::int64_t code, const PartScope &scope,
                           std::span<const Term> details) const {
    const auto number = build_.integer(code + (scope.part == Part::body ? BODY_BASE : 0));
    if (details.empty()) {
        fail(anno, number);
    }
    std::vector<Term> items{number};
    items.insert(items.end(), details.begin(), details.end());
    fail(anno, build_.tuple(items));
}

void Translator::define(const Term &definition) {
    Record record{.name = element(definition, 0), .fields = {}};
    std::int64_t position = 2;
    for (const auto &field : forms::elements(element(definition, 1))) {
        record.fields.push_back(record_field(build_, field, position++));
    }
    records_.push_back(std::move(record));
}

const Record *Translator::record(const Term &name) {
    const auto found = std::ranges::find_if(records_.rbegin(), records_.rend(),
                                            [&](const Record &r) { return r.name.word() == name.word(); });
    if (found == records_.rend()) {
        return nullptr;
    }
    used_.push_back(name);
    return &*found;
}

Term Translator::clause_list(const FunClauses &fun, Dialect dialect, const Bound &bound) {
    const auto &anno = fun.anno;
    std::vector<Term> specs;
    for (const auto &clause : forms::elements(fun.clauses)) {
        const auto guards = is_node(clause, "clause", 5) ? element(clause, 3) : build_.nil();
        if (!guards.is_cons() || !guards.tail_unchecked().is_cons()) {
            specs.push_back(ms_clause(clause, dialect, bound));
            continue;
        }
        // Alternative guards (;) give one clause each.
        for (const auto &guard : forms::elements(guards)) {
            const auto split = forms::with_element(build_, clause, 3, build_.list(std::array{guard}));
            specs.push_back(ms_clause(split, dialect, bound));
        }
    }
    auto list = build_.node("nil"_atom, anno);
    for (auto spec = specs.rbegin(); spec != specs.rend(); ++spec) {
        list = build_.node("cons"_atom, anno, *spec, list);
    }
    return list;
}

Term Translator::ms_clause(const Term &clause, Dialect dialect, const Bound &bound) {
    if (!is_node(clause, "clause", 5)) {
        no_clause();
    }
    const auto anno = element(clause, 1);
    const auto parameter = only(element(clause, 2));
    if (!parameter || !head_shape(*parameter, dialect)) {
        fail(anno, build_.integer(dialect == Dialect::ets ? ERR_ETS_HEAD : ERR_DBG_HEAD));
    }
    HeadScope scope{.names = {{build_.atom("_"_atom).word(), build_.atom("_"_atom)}}, .next = 1, .outer = &bound};
    const auto head = transform_head(*parameter, scope);
    const auto guards = transform_guards(anno, element(clause, 3), {.names = scope.names, .part = Part::guard});
    const auto body = part_list(anno, forms::elements(element(clause, 4)), {.names = scope.names, .part = Part::body});
    return build_.node("tuple"_atom, anno, build_.list(std::array{head, guards, body}));
}

// A variable matched with the whole pattern stands for '$_'; the other variables become '$N'.
Term Translator::transform_head(const Term &parameter, HeadScope &scope) {
    if (is_node(parameter, "match", 4)) {
        const auto left = element(parameter, 2);
        const auto right = element(parameter, 3);
        if (is_var(left)) {
            bind_object(left, scope);
            return head(right, scope);
        }
        if (is_var(right)) {
            bind_object(right, scope);
            return head(left, scope);
        }
    }
    return head(parameter, scope);
}

void Translator::bind_object(const Term &var, HeadScope &scope) {
    scope.names.insert(scope.names.begin(), {element(var, 2).word(), build_.atom("$_"_atom)});
    shadow_check(var, scope);
}

// A head variable bound outside the fun gets a warning.
void Translator::shadow_check(const Term &var, const HeadScope &scope) {
    const auto name = element(var, 2);
    if (!scope.outer->contains(name.word())) {
        return;
    }
    const auto reason = build_.tuple(std::array{build_.integer(WARN_SHADOW_VAR), name});
    warnings_.push_back(build_.tuple(std::array{anno_location(build_.context(), element(var, 1)), reason}));
}

// th/2: records, matches, '$N' atoms, bit syntax variables and variables first, then every child.
Term Translator::head(const Term &pattern, HeadScope &scope) {
    if (is_node(pattern, "record", 4)) {
        return head_record(pattern, scope);
    }
    if (is_node(pattern, "match", 4)) {
        fail(element(pattern, 1), build_.integer(ERR_HEADMATCH));
    }
    if (is_node(pattern, "atom", 3)) {
        return head_atom(pattern);
    }
    if (is_node(pattern, "bin_element", 5) && is_var(element(pattern, 2))) {
        const auto var = element(pattern, 2);
        fail(element(var, 1), build_.tuple(std::array{build_.integer(ERR_HEADBINMATCH), element(var, 2)}));
    }
    if (is_var(pattern)) {
        return head_var(pattern, scope);
    }
    if (is_node(pattern, "map_field_exact", 4)) {
        const auto key = head(element(pattern, 2), scope);
        const auto value = head(element(pattern, 3), scope);
        return build_.node("map_field_assoc"_atom, element(pattern, 1), key, value);
    }
    return head_children(pattern, scope);
}

// '$1', '$-1' and '$+1' are reserved in match specification heads.
Term Translator::head_atom(const Term &node) const {
    const auto text = spelling(element(node, 2));
    if (text.starts_with('$') && integer_text(text.substr(1))) {
        fail(element(node, 1), build_.tuple(std::array{build_.integer(ERR_HEADDOLLARATOM), element(node, 2)}));
    }
    return node;
}

Term Translator::head_var(const Term &node, HeadScope &scope) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    shadow_check(node, scope);
    const auto known = std::ranges::find_if(scope.names, [&](const auto &entry) { return entry.first == name.word(); });
    if (known != scope.names.end()) {
        return build_.atom_node(anno, known->second);
    }
    const auto translated = build_.atom("$" + std::to_string(scope.next++));
    scope.names.insert(scope.names.begin(), {name.word(), translated});
    return build_.atom_node(anno, translated);
}

// The children of a tuple or list, translated in order; other terms are kept.
Term Translator::head_children(const Term &term, HeadScope &scope) {
    if (term.is_tuple()) {
        std::vector<Term> items;
        items.reserve(term.tuple_size_unchecked());
        for (std::size_t index = 0; index < term.tuple_size_unchecked(); ++index) {
            items.push_back(head(element(term, index), scope));
        }
        return build_.tuple(items);
    }
    if (!term.is_cons()) {
        return term;
    }
    std::vector<Term> heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(head(rest.head_unchecked(), scope));
    }
    return build_.terms().list(heads, head(rest, scope));
}

// A record pattern as its tuple: unnamed fields are '_' or the value of the `_ = Value` field.
Term Translator::head_record(const Term &node, HeadScope &scope) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    GivenFields given;
    for (const auto &field : forms::elements(element(node, 3))) {
        const auto key = is_node(field, "record_field", 4) ? element(field, 2) : build_.nil();
        if (is_node(key, "atom", 3)) {
            given.named.emplace_back(element(key, 2), head(element(field, 3), scope));
        } else if (is_var(key) && is_atom(element(key, 2), "_")) {
            given.defaults.push_back(head(element(field, 3), scope));
        } else {
            fail(anno, build_.tuple(std::array{build_.integer(ERR_HEADBADREC), name}));
        }
    }
    const auto fallback =
        split_default(build_, anno, name, given, ERR_HEADMULTIFIELD).value_or(build_.atom_node(anno, "_"_atom));
    const auto *definition = record(name);
    if (definition == nullptr) {
        fail(anno, build_.tuple(std::array{build_.integer(ERR_HEADBADREC), name}));
    }
    std::vector<Term> values{build_.atom_node(anno, name)};
    for (const auto &field : definition->fields) {
        values.push_back(field_value(given, field, fallback));
    }
    check_fields(build_, anno, *definition, given, {.multi = ERR_HEADMULTIFIELD, .bad = ERR_HEADBADFIELD});
    return build_.node("tuple"_atom, anno, build_.list(values));
}

Term field_value(const GivenFields &given, const Field &field, const Term &fallback) {
    const auto found = std::ranges::find_if(given.named.rbegin(), given.named.rend(),
                                            [&](const auto &entry) { return entry.first.word() == field.name.word(); });
    return found == given.named.rend() ? fallback : found->second;
}

std::optional<Term> split_default(const Builder &build, const Term &anno, const Term &name, const GivenFields &given,
                                  std::int64_t multi) {
    if (given.defaults.size() > 1) {
        throw MsError{.anno = anno,
                      .reason = build.tuple(std::array{build.integer(multi), name, build.atom("_"_atom)})};
    }
    if (given.defaults.empty()) {
        return std::nullopt;
    }
    return given.defaults.front();
}

void check_fields(const Builder &build, const Term &anno, const Record &record, const GivenFields &given,
                  FieldCodes codes) {
    const auto &named = given.named;
    for (auto at = named.size(); at-- > 0;) {
        const auto twice =
            std::ranges::any_of(named.begin(), named.begin() + static_cast<std::ptrdiff_t>(at),
                                [&](const auto &entry) { return entry.first.word() == named[at].first.word(); });
        if (twice) {
            throw MsError{.anno = anno,
                          .reason = build.tuple(std::array{build.integer(codes.multi), record.name, named[at].first})};
        }
    }
    for (auto entry = named.rbegin(); entry != named.rend(); ++entry) {
        const auto known = std::ranges::any_of(
            record.fields, [&](const Field &field) { return field.name.word() == entry->first.word(); });
        if (!known) {
            throw MsError{.anno = anno,
                          .reason = build.tuple(std::array{build.integer(codes.bad), record.name, entry->first})};
        }
    }
}
} // namespace clause::runtime::builtins::front_end::ms
