#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>
#include <map>

// erl_lint's types and specs (plan 21 step LIB34C): type definitions, check_type/2 with its singleton type variable
// tracking, record types, specs, callbacks and optional callbacks.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// How often a type variable was seen: once, once in each of a union's alternatives, or several times.
enum class Seen : std::uint8_t { once, once_union, multiple };

struct SeenVar {
    Seen seen = Seen::once;
    Term anno;
};

// The type variables seen, by name.
using SeenVars = std::map<std::string_view, std::pair<Term, SeenVar>>;

// The combination of union alternatives' variables (check_type_2's maps:merge_with/3 fun).
SeenVar combine(const SeenVars &before, std::string_view name, const SeenVar &left, const SeenVar &right) {
    if (left.seen == Seen::once && right.seen == Seen::once) {
        return {.seen = before.contains(name) ? Seen::once : Seen::once_union, .anno = left.anno};
    }
    if (left.seen == Seen::once && right.seen == Seen::once_union) {
        return {.seen = Seen::once_union, .anno = left.anno};
    }
    if (left.seen == Seen::once_union && right.seen != Seen::multiple) {
        return left;
    }
    if (left.seen != Seen::multiple) {
        return right;
    }
    return left;
}
} // namespace

// The walk of check_type/2 over one type with its seen variables.
class TypeCheck final {
  public:
    explicit TypeCheck(Lint &lint) : lint_(lint) {}

    SeenVars vars;

    void type1(const Term &type) {
        if (is_node(type, "type", 4)) {
            const auto args = element(type, 3);
            const auto pair = lint_.fa(element(type, 2), args.is_list() ? lint_.list_length(args) : 0);
            if (lint_.types_.contains(pair)) {
                lint_.used_type(pair, element(type, 1));
            }
        }
        type2(type);
    }

    // An old abstract code record field type is an error that ends the check of the type.
    bool reject_old_abstract_code(const Term &type) {
        if (!type.is_cons() || !is_node(type.head_unchecked(), "typed_record_field", 3)) {
            return false;
        }
        lint_.add_error({element(element(type.head_unchecked(), 1), 1), lint_.atom("old_abstract_code"_atom)});
        return true;
    }

    // Types that need no check: integers, atoms and any().
    static bool is_leaf_type(const Term &type) {
        return is_node(type, "integer", 3) || is_node(type, "atom", 3) ||
               (is_node(type, "type", 3) && is_atom(element(type, 2), "any"));
    }

    void type2(const Term &type) {
        if (reject_old_abstract_code(type) || is_leaf_type(type)) {
            return;
        }
        if (is_node(type, "ann_type", 3)) {
            type1(forms::elements(element(type, 2)).at(1));
        } else if (is_node(type, "remote_type", 3) && is_remote_type(type)) {
            remote_type(type);
        } else if (is_node(type, "var", 3)) {
            variable(type);
        } else if (is_node(type, "type", 4)) {
            builtin(type);
        } else if (is_node(type, "user_type", 4)) {
            user_type(element(type, 1), element(type, 2), element(type, 3));
        } else if (!is_node(lint_.partial_eval(type), "integer", 3)) {
            lint_.add_error({element(type, 1), lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("integer"_atom))});
        }
    }

    void each1(const Term &list) {
        for (const auto &item : forms::elements(list)) {
            type1(item);
        }
    }

  private:
    // {remote_type, A, [{atom, _, Mod}, {atom, _, Name}, Args]}.
    static bool is_remote_type(const Term &type) {
        if (!element(type, 2).is_list()) {
            return false;
        }
        const auto parts = forms::elements(element(type, 2));
        return parts.size() == 3 && is_node(parts[0], "atom", 3) && is_node(parts[1], "atom", 3);
    }

    void remote_type(const Term &type) {
        const auto anno = element(type, 1);
        const auto parts = forms::elements(element(type, 2));
        const Remote remote{.module = element(parts[0], 2), .name = element(parts[1], 2)};
        lint_.check_module_name({anno, remote.module});
        lint_.deprecated_type(anno, remote, parts[2]);
        if (remote.module.word() == lint_.module_.word()) {
            user_type(anno, remote.name, parts[2]);
        } else {
            each1(parts[2]);
        }
    }

    void variable(const Term &type) {
        const auto name = element(type, 2);
        if (is_atom(name, "_")) {
            return;
        }
        const auto key = name.atom_spelling_unchecked();
        const auto found = vars.find(key);
        if (found == vars.end()) {
            vars.emplace(key, std::pair{name, SeenVar{.seen = Seen::once, .anno = element(type, 1)}});
        } else {
            found->second.second.seen = Seen::multiple;
        }
    }

    void user_type(const Term &anno, const Term &name, const Term &args) {
        lint_.used_type(lint_.fa(name, lint_.list_length(args)), anno);
        each1(args);
    }

    bool has_two_args(const AnnoTerm &use) { return lint_.list_length(use.term) == 2; }

    // The fun, range, map_field_assoc and binary types with two arguments; false for any other.
    bool two_argument_type(std::string_view spelling, const AnnoTerm &use) {
        if (spelling == "fun" && has_two_args(use)) {
            fun_type(use);
        } else if (spelling == "range" && has_two_args(use)) {
            range(use);
        } else if (spelling == "map_field_assoc" && has_two_args(use)) {
            each1(use.term);
        } else if (spelling == "binary" && has_two_args(use)) {
            binary(use);
        } else {
            return false;
        }
        return true;
    }

    void builtin(const Term &type) {
        const AnnoTerm use{element(type, 1), element(type, 3)};
        const auto name = element(type, 2);
        const auto spelling = name.is_atom() ? name.atom_spelling_unchecked() : std::string_view{};
        if (spelling == "bool" && use.term.is_nil()) {
            lint_.add_warning(
                {use.anno, lint_.triple(lint_.atom("renamed_type"_atom), name, lint_.atom("boolean"_atom))});
            return;
        }
        if (two_argument_type(spelling, use) ||
            ((spelling == "map" || spelling == "tuple") && is_atom(use.term, "any"))) {
            return;
        }
        other_builtin(spelling, {use.anno, name}, use.term);
    }

    // The builtin types without a fixed argument count: maps, records, products, unions and the predefined ones.
    void other_builtin(std::string_view spelling, const AnnoTerm &name, const Term &args) {
        if (spelling == "map") {
            for (const auto &pair : forms::elements(args)) {
                type2(pair);
            }
        } else if (spelling == "record" && args.is_cons()) {
            record({name.anno, args});
        } else if (spelling == "product" || spelling == "tuple") {
            each1(args);
        } else if (spelling == "union") {
            union_type(args);
        } else {
            generic(name, args);
        }
    }

    void fun_type(const AnnoTerm &use) {
        const auto domain = forms::elements(use.term)[0];
        const auto plain = is_node(domain, "type", 4) && is_atom(element(domain, 2), "product");
        const auto any = is_node(domain, "type", 3) && is_atom(element(domain, 2), "any");
        if (!plain && !any) {
            lint_.add_error({use.anno, lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("fun"_atom))});
        }
        each1(use.term);
    }

    void range(const AnnoTerm &use) {
        const auto parts = forms::elements(use.term);
        const auto from = lint_.partial_eval(parts[0]);
        const auto to = lint_.partial_eval(parts[1]);
        const auto valid = is_node(from, "integer", 3) && is_node(to, "integer", 3) &&
                           collections::compare(element(from, 2), element(to, 2)) < 0;
        if (!valid) {
            lint_.add_error({use.anno, lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("range"_atom))});
        }
    }

    void binary(const AnnoTerm &use) {
        const auto parts = forms::elements(use.term);
        const auto base = lint_.partial_eval(parts[0]);
        const auto unit = lint_.partial_eval(parts[1]);
        const auto zero = lint_.build_.integer(0);
        const auto valid = is_node(base, "integer", 3) && is_node(unit, "integer", 3) &&
                           collections::compare(element(base, 2), zero) >= 0 &&
                           collections::compare(element(unit, 2), zero) >= 0;
        if (!valid) {
            lint_.add_error({use.anno, lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("binary"_atom))});
        }
    }

    void record(const AnnoTerm &use) {
        const auto parts = forms::elements(use.term);
        const auto name = parts[0];
        const std::vector fields(parts.begin() + 1, parts.end());
        if (is_node(name, "atom", 3)) {
            lint_.used_record(element(name, 2));
            record_types(use.anno, element(name, 2), fields);
            return;
        }
        const auto items = is_node(name, "tuple", 3) ? forms::elements(element(name, 2)) : std::vector<Term>{};
        if (items.size() == 2 && is_node(items[0], "atom", 3) && is_node(items[1], "atom", 3)) {
            if (element(items[0], 2).word() == lint_.module_.word()) {
                lint_.used_record(element(items[1], 2));
                record_types(use.anno, element(items[1], 2), fields);
            }
            return;
        }
        lint_.add_error({use.anno, lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("record"_atom))});
    }

    void record_types(const Term &anno, const Term &name, const std::vector<Term> &fields) {
        const auto found = lint_.records_.find(name);
        if (found == lint_.records_.end()) {
            if (!lint_.rec_imports_.contains(name)) {
                lint_.suggest_record_name(anno, "undefined_record"_atom, name);
            }
            return;
        }
        if (found->second.flavor == Flavor::native) {
            if (!fields.empty()) {
                lint_.add_error({element(fields[0], 1), lint_.atom("native_record_field_types"_atom)});
            }
            return;
        }
        if (!std::ranges::all_of(fields, [](const Term &field) {
                return is_node(field, "type", 4) && is_atom(element(field, 2), "field_type");
            })) {
            lint_.add_error({anno, lint_.pair(lint_.atom("type_syntax"_atom), lint_.atom("record"_atom))});
            return;
        }
        field_types(name, fields, found->second);
    }

    void field_types(const Term &name, const std::vector<Term> &fields, const RecordDef &record) {
        const auto &defined = record.fields;
        std::vector<Term> seen;
        for (const auto &field : fields) {
            const auto parts = forms::elements(element(field, 3));
            if (parts.size() != 2 || !is_node(parts[0], "atom", 3)) {
                lists::no_clause();
            }
            const auto anno = element(parts[0], 1);
            const auto field_name = element(parts[0], 2);
            const auto exists = std::ranges::any_of(defined, [&](const Term &known) {
                return is_node(known, "record_field", 4) && is_node(element(known, 2), "atom", 3) &&
                       element(element(known, 2), 2).word() == field_name.word();
            });
            if (!exists) {
                lint_.suggest_field_name(anno, name, field_name, defined);
            }
            if (std::ranges::any_of(seen, [&](const Term &known) { return known.word() == field_name.word(); })) {
                lint_.add_error({anno, lint_.triple(lint_.atom("redefine_field"_atom), name, field_name)});
            }
            type2(parts[1]);
            seen.push_back(field_name);
        }
    }

    void union_type(const Term &args) {
        const auto before = vars;
        auto merged = vars;
        for (const auto &alternative : forms::elements(args)) {
            vars = before;
            type1(alternative);
            for (const auto &[key, entry] : vars) {
                const auto found = merged.find(key);
                if (found == merged.end()) {
                    merged.emplace(key, entry);
                } else {
                    found->second.second = combine(before, key, found->second.second, entry.second);
                }
            }
        }
        vars = std::move(merged);
    }

    void generic(const AnnoTerm &name, const Term &args) {
        const auto arity = args.is_list() ? lint_.list_length(args) : 0;
        if (name.term.is_atom() && is_predefined_type(name.term.atom_spelling_unchecked(), arity)) {
            lint_.used_type(lint_.fa(name.term, arity), name.anno);
        }
        each1(args);
    }

    Lint &lint_;
};

void Lint::check_type(const Term &type) {
    TypeCheck check(*this);
    check.type1(type);
    for (const auto &[name, entry] : check.vars) {
        if (entry.second.seen != Seen::multiple && !name.starts_with('_')) {
            add_error({entry.second.anno, pair(atom("singleton_typevar"_atom), entry.first)});
        }
    }
}

void Lint::deprecated_type(const Term &anno, const Remote &type, const Term &args) {
    const auto arity = list_length(args);
    const auto info = obsolete_type(*this, type.module, type.name, arity);
    if (!info) {
        return;
    }
    const auto mna = triple(type.module, type.name, build_.integer(arity));
    if (is_atom(element(*info, 0), "deprecated") && element(*info, 1).is_list()) {
        maybe_add_warning(anno, triple(atom("deprecated_type"_atom), mna, element(*info, 1)));
    } else if (is_atom(element(*info, 0), "removed")) {
        add_warning({anno, triple(atom("removed_type"_atom), mna, element(*info, 1))});
    }
}

void Lint::type_def(const Term &attr, const AnnoTerm &declared) {
    const auto &anno = declared.anno;
    const auto name = element(declared.term, 0);
    const auto args = element(declared.term, 2);
    const auto type_pair = fa(name, list_length(args));
    const auto builtin =
        name.is_atom() && is_predefined_type(name.atom_spelling_unchecked(), list_length(args)) &&
        std::ranges::none_of(compile_list_, [](const Term &option) { return is_atom(option, "no_auto_import_types"); });
    if (!builtin && types_.contains(type_pair)) {
        add_error({anno, pair(atom("redefine_type"_atom), type_pair)});
        return;
    }
    types_.insert_or_assign(type_pair, TypeInfo{.attr = attr, .anno = anno});
    type_id_ = pair(atom("type"_atom), type_pair);
    std::vector<Term> parts{element(declared.term, 1)};
    const auto listed = forms::elements(args);
    parts.insert(parts.end(), listed.begin(), listed.end());
    check_type(build_.node("type"_atom, nowarn_anno(), atom("product"_atom), build_.list(parts)));
    if (builtin && warn_enabled("redefined_builtin_type") && !nowarn_builtin(type_pair)) {
        add_warning({anno, pair(atom("redefine_builtin_type"_atom), type_pair)});
    }
}

// The types listed by nowarn_redefined_builtin_type options.
bool Lint::nowarn_builtin(const Term &type_pair) const {
    for (const auto &option : compile_list_) {
        if (!is_pair(option) || !is_atom(element(option, 0), "nowarn_redefined_builtin_type")) {
            continue;
        }
        std::vector<Term> pending{element(option, 1)};
        while (!pending.empty()) {
            const auto item = pending.back();
            pending.pop_back();
            if (item.is_list()) {
                const auto items = forms::elements(item);
                pending.insert(pending.end(), items.begin(), items.end());
            } else if (collections::same(item, type_pair)) {
                return true;
            }
        }
    }
    return false;
}

void Lint::spec_decl(const AnnoTerm &declared) {
    const auto &anno = declared.anno;
    const auto function = element(declared.term, 0);
    const auto three = function.is_tuple() && function.tuple_size_unchecked() == 3;
    if (!three && !is_pair(function)) {
        lists::no_clause();
    }
    const auto arity = element(function, three ? 2 : 1);
    const auto mfa = three ? function : triple(module_, element(function, 0), arity);
    const auto existed = specs_.contains(mfa);
    specs_.insert_or_assign(mfa, anno);
    if (existed) {
        add_error({anno, pair(atom("redefine_spec"_atom), function)});
        return;
    }
    if (element(mfa, 0).word() != module_.word()) {
        if (three) {
            check_module_name({anno, element(function, 0)});
        }
        add_error({anno, pair(atom("bad_module"_atom), mfa)});
    }
    type_id_ = pair(atom("spec"_atom), mfa);
    check_specs(element(declared.term, 1), "spec_wrong_arity"_atom, arity);
}

void Lint::callback_decl(const AnnoTerm &declared) {
    const auto &anno = declared.anno;
    const auto function = element(declared.term, 0);
    if (function.is_tuple() && function.tuple_size_unchecked() == 3) {
        check_module_name({anno, element(function, 0)});
        add_error({anno, pair(atom("bad_callback"_atom), function)});
        return;
    }
    if (!is_pair(function)) {
        lists::no_clause();
    }
    const auto mfa = triple(module_, element(function, 0), element(function, 1));
    const auto existed = callbacks_.contains(mfa);
    callbacks_.insert_or_assign(mfa, anno);
    if (existed) {
        add_error({anno, pair(atom("redefine_callback"_atom), function)});
        return;
    }
    type_id_ = pair(atom("spec"_atom), mfa);
    check_specs(element(declared.term, 1), "callback_wrong_arity"_atom, element(function, 1));
}

// A spec split into its function type and the types its bounded_fun constraints list.
struct SpecParts {
    Term fun_type;
    std::vector<Term> constraint_types;
};

namespace {
// The types of a bounded_fun constraint with two arguments, none for another form.
std::vector<Term> constraint_types(const Term &constraint) {
    if (!is_node(constraint, "type", 4) || !is_atom(element(constraint, 2), "constraint")) {
        return {};
    }
    const auto args = forms::elements(element(constraint, 3));
    return args.size() == 2 ? forms::elements(args[1]) : std::vector<Term>{};
}

SpecParts spec_parts(const Term &spec) {
    if (!is_node(spec, "type", 4) || !is_atom(element(spec, 2), "bounded_fun")) {
        return {.fun_type = spec, .constraint_types = {}};
    }
    const auto parts = forms::elements(element(spec, 3));
    SpecParts out{.fun_type = parts.at(0), .constraint_types = {}};
    for (const auto &constraint : forms::elements(parts.at(1))) {
        const auto types = constraint_types(constraint);
        out.constraint_types.insert(out.constraint_types.end(), types.begin(), types.end());
    }
    return out;
}
} // namespace

// check_specs/4 for one spec: its function type has the declared arity, then the types are checked.
void Lint::check_spec(const Term &spec, AtomName error, const Term &arity) {
    const auto [fun_type, constraints] = spec_parts(spec);
    const auto fun_parts = is_node(fun_type, "type", 4) ? forms::elements(element(fun_type, 3)) : std::vector<Term>{};
    if (!is_atom(element(fun_type, 2), "fun") || fun_parts.size() != 2 || !is_node(fun_parts[0], "type", 4)) {
        lists::no_clause();
    }
    const auto domain = forms::elements(element(fun_parts[0], 3));
    if (!collections::same(build_.integer(static_cast<std::int64_t>(domain.size())), arity)) {
        add_error({element(fun_type, 1), atom(error)});
    }
    std::vector<Term> parts{fun_type};
    parts.insert(parts.end(), constraints.begin(), constraints.end());
    check_type(build_.node("type"_atom, nowarn_anno(), atom("product"_atom), build_.list(parts)));
}

void Lint::check_specs(const Term &specs, AtomName error, const Term &arity) {
    for (const auto &spec : forms::elements(specs)) {
        check_spec(spec, error, arity);
    }
}

void Lint::optional_callbacks(const AnnoTerm &declared) {
    const auto &value = declared.term;
    if (!value.is_list()) {
        return;
    }
    const auto items = forms::elements(value);
    const auto valid = std::ranges::all_of(items, [](const Term &item) {
        return is_pair(item) && element(item, 0).is_atom() && element(item, 1).is_integer() &&
               string::small_of(element(item, 1)).value_or(0) >= 0;
    });
    if (!valid) {
        return;
    }
    for (const auto &item : items) {
        const auto mfa = triple(module_, element(item, 0), element(item, 1));
        const auto existed = optional_callbacks_.contains(mfa);
        optional_callbacks_.insert_or_assign(mfa, declared.anno);
        if (existed) {
            add_error({declared.anno, pair(atom("redefine_optional_callback"_atom), item)});
        }
    }
}

} // namespace clause::runtime::builtins::front_end::lint
