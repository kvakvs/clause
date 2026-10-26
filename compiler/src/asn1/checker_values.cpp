#include "checker_internal.hpp"
#include <array>

// Added for the ASN.1 compiler (plan 21 step LIB84): value notation (X.680 clauses 17-41) normalized as asn1ct's
// normalize_value does: named INTEGERs to numbers, named bits to atom lists, strings to lists, missing DEFAULT
// components to their defaults.
namespace clause::asn1::detail {
namespace {
// Values of the well-known OBJECT IDENTIFIER arcs (X.660 Annex A).
std::optional<BigInt> well_known_arc(const std::vector<Term> &prefix, const std::string_view name) {
    struct Arc {
        int parent_;
        std::string_view name_;
        int value_;
    };

    static constexpr std::array ARCS = {Arc{-1, "itu-t", 0},
                                        Arc{-1, "ccitt", 0},
                                        Arc{-1, "iso", 1},
                                        Arc{-1, "joint-iso-itu-t", 2},
                                        Arc{-1, "joint-iso-ccitt", 2},
                                        Arc{0, "recommendation", 0},
                                        Arc{0, "question", 1},
                                        Arc{0, "administration", 2},
                                        Arc{0, "network-operator", 3},
                                        Arc{0, "identified-organization", 4},
                                        Arc{1, "standard", 0},
                                        Arc{1, "registration-authority", 1},
                                        Arc{1, "member-body", 2},
                                        Arc{1, "identified-organization", 3}};
    int parent = -1;
    if (prefix.size() == 1) {
        parent = std::stoi(prefix.front().text_);
    } else if (!prefix.empty()) {
        return std::nullopt;
    }
    for (const auto &arc : ARCS) {
        if (arc.parent_ == parent && arc.name_ == name) {
            return BigInt(arc.value_);
        }
    }
    return std::nullopt;
}

// Append the `width` low bits of `value`, most significant first, to a bitstring term.
void append_bits(Term &result, const unsigned value, const unsigned width) {
    for (unsigned i = 0; i < width; ++i, ++result.bits_) {
        if (result.bits_ % 8 == 0) {
            result.text_.push_back('\0');
        }
        if (((value >> (width - 1 - i)) & 1U) != 0) {
            result.text_.back() = static_cast<char>(result.text_.back() | (0x80 >> (result.bits_ % 8)));
        }
    }
}

Term bits_term(const std::string &digits, const bool hex) {
    Term result;
    result.kind_ = Term::Kind::bitstring;
    result.bits_ = 0;
    for (const char digit : digits) {
        const unsigned value = hex ? static_cast<unsigned>(std::stoul(std::string(1, digit), nullptr, 16))
                                   : static_cast<unsigned>(digit - '0');
        append_bits(result, value, hex ? 4 : 1);
    }
    return result;
}

BigInt to_integer(const Term &term) { return BigInt(term.text_); }
} // namespace

// Parses value notation once its governor is known.
class ValueChecker {
  public:
    ValueChecker(Checker &checker, const Scope &scope, Parser &parser)
        : checker_(checker), scope_(scope), parser_(parser) {}

    Term value(NodeId type);
    BigInt integer_value(const std::vector<std::pair<std::string, BigInt>> &named);
    std::optional<BigInt> named_number(const std::vector<std::pair<std::string, BigInt>> &named);

  private:
    bool reference_ahead() const;
    std::optional<Term> referenced(NodeId type);
    std::optional<Term> reference_value();
    Term object_value(const TypeSyntax &reference);
    Term boolean(const Node &node);
    Term integer(const Node &node);
    Term enumerated(const Node &node);
    Term null(const Node &node);
    Term bit_string(const Node &node);
    Term octet_string(const Node &node);
    Term object_identifier(const Node &node);
    Term real(const Node &node);
    Term string(const Node &node);
    Term sequence(const Node &node);
    Term collection(const Node &node);
    Term choice(const Node &node);
    Term open_type(const Node &node);
    Term unsupported(const Node &node);
    std::vector<Term> arcs();
    void arc(std::vector<Term> &arcs);
    std::vector<std::pair<std::string, TokenRange>> named_values();
    Term component_value(const Component &component, const std::vector<std::pair<std::string, TokenRange>> &given);

    [[noreturn]] void fail(const std::string &message) const { parser_.fail(message); }

    Checker &checker_;
    const Scope &scope_;
    Parser &parser_;
};

Term ValueChecker::value(const NodeId type) {
    if (auto found = referenced(type)) {
        return *found;
    }
    // A copy: checking component values may add nodes, which moves the node vector.
    const auto node = checker_.content(type);
    using Method = Term (ValueChecker::*)(const Node &);
    // Indexed by NodeKind, in its declaration order.
    static constexpr std::array<Method, 20> METHODS = {&ValueChecker::boolean,
                                                       &ValueChecker::integer,
                                                       &ValueChecker::enumerated,
                                                       &ValueChecker::null,
                                                       &ValueChecker::bit_string,
                                                       &ValueChecker::octet_string,
                                                       &ValueChecker::object_identifier,
                                                       &ValueChecker::object_identifier,
                                                       &ValueChecker::real,
                                                       &ValueChecker::string,
                                                       &ValueChecker::string,
                                                       &ValueChecker::string,
                                                       &ValueChecker::string,
                                                       &ValueChecker::sequence,
                                                       &ValueChecker::sequence,
                                                       &ValueChecker::collection,
                                                       &ValueChecker::collection,
                                                       &ValueChecker::choice,
                                                       &ValueChecker::open_type,
                                                       &ValueChecker::unsupported};
    return (this->*METHODS[static_cast<std::size_t>(node.kind_)])(node);
}

// True at a value reference: `name` (not a choice value `name : v`) or `Module.name`.
bool ValueChecker::reference_ahead() const {
    if (parser_.is_kind(TokenKind::upper) && parser_.is(".", 1) && parser_.is_kind(TokenKind::lower, 2)) {
        return true;
    }
    return parser_.is_kind(TokenKind::lower) && !parser_.is(":", 1);
}

// The value a value reference or value parameter names, unless the identifier is an item of the type's own
// notation (an enumeration item or named number).
std::optional<Term> ValueChecker::referenced(const NodeId type) {
    if (!reference_ahead()) {
        return std::nullopt;
    }
    const auto &node = checker_.content(type);
    if (node.kind_ == NodeKind::enumerated) {
        return std::nullopt;
    }
    for (const auto &named : node.named_numbers_) {
        if (named.first == parser_.peek().text_) {
            return std::nullopt;
        }
    }
    return reference_value();
}

std::optional<Term> ValueChecker::reference_value() {
    const auto start = parser_.position();
    auto defined = parser_.defined();
    if (!defined->fields_.empty()) {
        return object_value(*defined);
    }
    if (defined->parameterized_) {
        checker_.fail(scope_, start, "parameterized values are not supported");
    }
    const auto &reference = defined->reference_;
    const auto found = checker_.find(reference, scope_);
    if (found.binding_ != nullptr) {
        const auto &binding = *found.binding_;
        if (binding.kind_ != Binding::Kind::value) {
            fail(reference.name_ + " is not a value");
        }
        const auto governor = checker_.type(*binding.type_.syntax_, binding.type_.scope_, {});
        return checker_.value(binding.range_, governor, binding.scope_);
    }
    if (found.assignment_->kind_ != AssignmentSyntax::Kind::value || found.assignment_->parameterized_) {
        fail(reference.name_ + " is not a value");
    }
    return checker_.named_value(found);
}

// `object.&field`: the value an object's value field is set to (X.681 15 ValueFromObject).
Term ValueChecker::object_value(const TypeSyntax &reference) {
    auto base = reference;
    const auto field = base.fields_.back();
    base.fields_.pop_back();
    const auto object = checker_.named_object(base, nullptr, scope_);
    const auto setting = object->settings_.find(field);
    if (setting == object->settings_.end() ||
        (setting->second.kind_ != FieldKind::fixed_value && setting->second.kind_ != FieldKind::variable_value)) {
        fail("no value in field " + field);
    }
    return setting->second.value_;
}

Term ValueChecker::boolean(const Node & /*node*/) {
    if (parser_.accept("TRUE")) {
        return Term::atom("true");
    }
    parser_.expect("FALSE");
    return Term::atom("false");
}

Term ValueChecker::integer(const Node &node) { return Term::integer(integer_value(node.named_numbers_)); }

BigInt ValueChecker::integer_value(const std::vector<std::pair<std::string, BigInt>> &named) {
    const bool negative = parser_.accept("-");
    if (parser_.is_kind(TokenKind::number)) {
        const BigInt magnitude(parser_.advance().text_);
        return negative ? BigInt(-magnitude) : magnitude;
    }
    if (negative) {
        fail("expected a number after '-'");
    }
    if (auto number = named_number(named)) {
        return *number;
    }
    auto found = reference_ahead() ? reference_value() : std::nullopt;
    if (!found || found->kind_ != Term::Kind::integer) {
        fail("expected an INTEGER value");
    }
    return to_integer(*found);
}

// A name of the INTEGER type's named numbers, which take precedence over value references.
std::optional<BigInt> ValueChecker::named_number(const std::vector<std::pair<std::string, BigInt>> &named) {
    if (!parser_.is_kind(TokenKind::lower)) {
        return std::nullopt;
    }
    for (const auto &[name, number] : named) {
        if (name == parser_.peek().text_) {
            parser_.advance();
            return number;
        }
    }
    return std::nullopt;
}

Term ValueChecker::enumerated(const Node &node) {
    const auto name = parser_.expect_kind(TokenKind::lower, "an enumeration item");
    for (const auto &item : node.named_numbers_) {
        if (item.first == name) {
            return Term::atom(name);
        }
    }
    fail("undefined enumeration item " + name);
}

Term ValueChecker::null(const Node & /*node*/) {
    parser_.expect("NULL");
    return Term::atom("NULL");
}

// '0101'B, '0F'H or a list of named bits.
Term ValueChecker::bit_string(const Node & /*node*/) {
    if (parser_.is_kind(TokenKind::bstring) || parser_.is_kind(TokenKind::hstring)) {
        const bool hex = parser_.is_kind(TokenKind::hstring);
        return bits_term(parser_.advance().text_, hex);
    }
    parser_.expect("{");
    std::vector<Term> names;
    while (!parser_.accept("}")) {
        names.push_back(Term::atom(parser_.expect_kind(TokenKind::lower, "a named bit")));
        parser_.accept(",");
    }
    return Term::list(std::move(names));
}

Term ValueChecker::octet_string(const Node & /*node*/) {
    if (!parser_.is_kind(TokenKind::bstring) && !parser_.is_kind(TokenKind::hstring)) {
        fail("expected a bstring or hstring");
    }
    const bool hex = parser_.is_kind(TokenKind::hstring);
    auto bits = bits_term(parser_.advance().text_, hex);
    bits.kind_ = Term::Kind::binary;
    bits.bits_ = 0;
    return bits;
}

Term ValueChecker::object_identifier(const Node & /*node*/) { return Term::tuple(arcs()); }

std::vector<Term> ValueChecker::arcs() {
    parser_.expect("{");
    std::vector<Term> result;
    while (!parser_.accept("}")) {
        arc(result);
    }
    return result;
}

// `number`, `name(number)`, a well-known `name`, or a defined value: an OBJECT IDENTIFIER first, else an INTEGER.
void ValueChecker::arc(std::vector<Term> &arcs) {
    if (parser_.is_kind(TokenKind::number)) {
        arcs.push_back(Term::integer(BigInt(parser_.advance().text_)));
        return;
    }
    const auto name = parser_.expect_kind(TokenKind::lower, "an OBJECT IDENTIFIER component");
    if (parser_.accept("(")) {
        arcs.push_back(Term::integer(integer_value({})));
        parser_.expect(")");
        return;
    }
    const Reference reference{"", name, parser_.position() - 1};
    if (const auto found = checker_.try_find(reference, scope_); found && found->assignment_ != nullptr) {
        const auto value = checker_.named_value(*found);
        if (value.kind_ == Term::Kind::tuple) {
            arcs.insert(arcs.end(), value.items_.begin(), value.items_.end());
        } else {
            arcs.push_back(value);
        }
        return;
    }
    if (const auto known = well_known_arc(arcs, name)) {
        arcs.push_back(Term::integer(*known));
        return;
    }
    fail("undefined OBJECT IDENTIFIER component " + name);
}

// A number, or `{mantissa m, base b, exponent e}`, kept as asn1ct keeps REAL values: unchanged.
Term ValueChecker::real(const Node & /*node*/) {
    if (parser_.is("PLUS-INFINITY") || parser_.is("MINUS-INFINITY")) {
        return Term::atom(parser_.advance().text_);
    }
    const bool negative = parser_.accept("-");
    if (parser_.is_kind(TokenKind::number) || parser_.is_kind(TokenKind::real)) {
        Term result{Term::Kind::real, (negative ? "-" : "") + parser_.advance().text_, 0, {}};
        if (result.text_.find_first_of(".eE") == std::string::npos) {
            result.kind_ = Term::Kind::integer;
        }
        return result;
    }
    fail("unsupported REAL value notation");
}

Term ValueChecker::string(const Node & /*node*/) {
    return Term::string(parser_.expect_kind(TokenKind::cstring, "a character string"));
}

std::vector<std::pair<std::string, TokenRange>> ValueChecker::named_values() {
    parser_.expect("{");
    std::vector<std::pair<std::string, TokenRange>> given;
    while (!parser_.accept("}")) {
        auto name = parser_.expect_kind(TokenKind::lower, "a component name");
        given.emplace_back(std::move(name), parser_.value());
        parser_.accept(",");
    }
    return given;
}

// `{ name value, ... }` as the record asn1ct builds: asn1_NOVALUE for missing OPTIONAL components, the default for
// missing DEFAULT ones.
Term ValueChecker::sequence(const Node &node) {
    const auto given = named_values();
    std::vector<Term> fields{Term::atom(node.record_)};
    for (const auto &component : node.components_) {
        fields.push_back(component_value(component, given));
    }
    return Term::tuple(std::move(fields));
}

Term ValueChecker::component_value(const Component &component,
                                   const std::vector<std::pair<std::string, TokenRange>> &given) {
    for (const auto &[name, range] : given) {
        if (name == component.name_) {
            return checker_.value(range, component.type_, scope_);
        }
    }
    if (component.default_) {
        return *component.default_;
    }
    if (component.presence_ == Presence::required) {
        fail("missing component " + component.name_);
    }
    return Term::atom("asn1_NOVALUE");
}

Term ValueChecker::collection(const Node &node) {
    parser_.expect("{");
    std::vector<Term> items;
    while (!parser_.accept("}")) {
        const auto range = parser_.value();
        items.push_back(checker_.value(range, node.element_, scope_));
        parser_.accept(",");
    }
    return Term::list(std::move(items));
}

Term ValueChecker::choice(const Node &node) {
    const auto name = parser_.expect_kind(TokenKind::lower, "a CHOICE alternative");
    parser_.expect(":");
    for (const auto &alternative : node.components_) {
        if (alternative.name_ == name) {
            return Term::tuple({Term::atom(name), value(alternative.type_)});
        }
    }
    fail("undefined CHOICE alternative " + name);
}

// `Type : value` of an open type: the value of that type.
Term ValueChecker::open_type(const Node & /*node*/) {
    auto type = parser_.type();
    parser_.expect(":");
    return value(checker_.type(*type, scope_, {}));
}

Term ValueChecker::unsupported(const Node & /*node*/) { fail("unsupported value notation"); }

Term Checker::value(const TokenRange range, const NodeId governor, const Scope &scope) {
    auto parser = this->parser(scope, range);
    auto result = ValueChecker(*this, scope, parser).value(governor);
    if (!parser.at_end()) {
        parser.fail("unexpected tokens after a value");
    }
    return result;
}

BigInt Checker::integer(const TokenRange range, const Scope &scope,
                        const std::vector<std::pair<std::string, BigInt>> &named) {
    auto parser = this->parser(scope, range);
    // Kept as text while the end is checked: no number is alive when the check throws.
    const auto text = ValueChecker(*this, scope, parser).integer_value(named).str();
    if (!parser.at_end()) {
        parser.fail("unexpected tokens after a number");
    }
    return BigInt(text);
}

Term Checker::named_value(const Found &found) {
    const auto &assignment = *found.assignment_;
    const std::pair key{found.scope_.module_->syntax_->name_, assignment.name_};
    if (const auto known = named_values_.find(key); known != named_values_.end()) {
        return known->second;
    }
    if (!values_in_progress_.insert(key).second) {
        fail(found.scope_, assignment.token_, "value " + assignment.name_ + " is defined in terms of itself");
    }
    Context context;
    context.path_.push_back({assignment.name_, false});
    const auto governor = type(*assignment.governor_, found.scope_, context);
    auto result = value(assignment.value_, governor, found.scope_);
    named_values_.emplace(key, result);
    values_in_progress_.erase(key);
    return result;
}
} // namespace clause::asn1::detail
