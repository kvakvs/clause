#include "../string/string.hpp"
#include "calls.hpp"
#include "public_key.hpp"
#include <algorithm>

// Added for public_key's names (plan 21 step LIB80E3): pubkey_cert's name normalisation and comparison, the name
// constraint rules of match_name/3 and the https wildcard match of pkix_verify_hostname_match_fun/1. The string,
// unicode and uri_string calls go to their builtins, so grapheme handling and errors stay OTP's.
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;
using Reason = abi::v1::ErrorReason;
using string::same;

namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term element(const Term &tuple, const std::size_t index) { return tuple.tuple_element_unchecked(index); }

// {Tag, Value} with an atom tag.
bool tagged(const Term &term, const std::string_view tag) {
    return term.is_tuple() && term.tuple_size_unchecked() == 2 && atom_is(element(term, 0), tag);
}

bool is_code(const Term &term, const char code) { return string::is_char(term, static_cast<unsigned char>(code)); }

// [Code | _].
bool starts_with(const Term &term, const char code) { return term.is_cons() && is_code(term.head_unchecked(), code); }

[[noreturn]] void no_clause() { erlang_error(Reason::function_clause); }

// The elements of a proper list, or none.
std::optional<std::vector<Term>> elements(const Term &list) {
    std::vector<Term> found;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        found.push_back(rest.head_unchecked());
    }
    return rest.is_nil() ? std::optional(found) : std::nullopt;
}

class Names;
// A rule of match_name/3: whether a name matches one permitted name.
using Rule = bool (*)(Names &, const Term &, const Term &);

class Names {
  public:
    explicit Names(ProcessContext &context) : context_(context), builder_(context) {}

    asn1::Builder &builder() { return builder_; }

    Term chars(const std::string_view text) { return builder_.string(std::as_bytes(std::span(text))); }

    Term call(const std::string_view module, const std::string_view function, std::initializer_list<Term> args) {
        return public_key::call(context_, module, function, args);
    }

    bool equal_ignoring_case(const Term &a, const Term &b) {
        return atom_is(call("string", "equal", {a, b, builder_.atom("true")}), "true");
    }

    Term casefold(const Term &text) { return call("string", "casefold", {text}); }

    // lists:map/2 of a walk: function_clause for anything but a proper list.
    template <typename Each> Term map(const Term &list, Each &&each) {
        const auto items = elements(list);
        if (!items) {
            no_clause();
        }
        std::vector<Term> mapped;
        mapped.reserve(items->size());
        for (const auto &item : *items) {
            mapped.push_back(each(item));
        }
        return builder_.list(mapped);
    }

    // lists:reverse/1: badarg for anything but a proper list, as the lists:reverse/2 BIF under it.
    Term reversed(const Term &list) {
        auto items = elements(list);
        if (!items) {
            erlang_error(Reason::badarg);
        }
        std::ranges::reverse(*items);
        return builder_.list(*items);
    }

    // normalize_general_name/1: printable string attributes trimmed, white space runs made single, case folded.
    Term normalize(const Term &name) {
        if (!tagged(name, "rdnSequence")) {
            no_clause();
        }
        return builder_.tuple(
            {element(name, 0), map(element(name, 1), [this](const Term &rdn) { return normalized_rdn(rdn); })});
    }

    bool is_issuer(const Term &issuer, const Term &candidate) {
        if (!tagged(issuer, "rdnSequence") || !tagged(candidate, "rdnSequence")) {
            no_clause();
        }
        return is_dir_name(element(normalize(issuer), 1), element(normalize(candidate), 1), builder_.atom("true"));
    }

    bool is_dir_name(Term first, Term second, const Term &exact);
    bool match_name(const Named<FlagRole> &type, const Named<NameRole> &name, const Named<CandidateRole> &permitted);
    bool is_suffix(const Term &suffix, const Term &text);
    bool is_double_quoted(const Term &text);

  private:
    Term normalized_rdn(const Term &rdn) {
        if (!rdn.is_cons() || !rdn.tail_unchecked().is_nil()) {
            return rdn;
        }
        const auto attribute = rdn.head_unchecked();
        if (!attribute.is_tuple() || attribute.tuple_size_unchecked() != 3 ||
            !tagged(element(attribute, 2), "printableString")) {
            return rdn;
        }
        const auto value = casefold(strip_spaces(element(element(attribute, 2), 1), false));
        const auto printable = builder_.tuple({element(element(attribute, 2), 0), value});
        const auto normalized = builder_.tuple({element(attribute, 0), element(attribute, 1), printable});
        return builder_.list(std::span(&normalized, 1));
    }

    // What one pair of RDNs does to is_dir_name/3's walk.
    enum class Step : std::uint8_t { next, unequal, stop };
    Step rdn_step(const Term &left, const Term &right);

    // strip_spaces/2: trimmed, runs of two spaces or more made one; deep chardata when KeepDeep, else a flat list.
    Term strip_spaces(const Term &text, bool keep_deep);
    bool is_dir_name2(const Term &first, const Term &second);
    bool any(const Named<NameRole> &name, const Named<CandidateRole> &permitted, Rule rule);

    ProcessContext &context_;
    asn1::Builder builder_;
};

Term Names::strip_spaces(const Term &text, const bool keep_deep) {
    const auto trimmed = call("string", "trim", {text});
    const auto parts = call("string", "split", {trimmed, chars("  "), builder_.atom("all")});
    if (parts.is_cons() && parts.tail_unchecked().is_nil()) {
        return parts.head_unchecked();
    }
    std::vector<Term> joined;
    const auto space = chars(" ");
    for (const auto &part : elements(parts).value_or(std::vector<Term>{})) {
        if (part.is_nil()) {
            continue;
        }
        if (!joined.empty()) {
            joined.push_back(space);
        }
        joined.push_back(call("string", "trim", {part, builder_.atom("leading"), space}));
    }
    const auto deep = builder_.list(joined);
    return keep_deep ? deep : call("unicode", "characters_to_list", {deep});
}

// Attribute values equal as terms, or as printable or UTF-8 strings compared without case and extra white space.
bool Names::is_dir_name2(const Term &first, const Term &second) {
    if (same(first, second)) {
        return true;
    }
    if (tagged(first, "printableString") || tagged(first, "utf8String")) {
        return is_dir_name2(element(first, 1), second);
    }
    if (tagged(second, "printableString") || tagged(second, "utf8String")) {
        return is_dir_name2(first, element(second, 1));
    }
    const auto text = [](const Term &term) { return term.is_list() || term.is_binary(); };
    if (text(first) && text(second)) {
        return equal_ignoring_case(strip_spaces(first, true), strip_spaces(second, true));
    }
    return false;
}

// [{'AttributeTypeAndValue', Type, Value}]: an RDN of one attribute.
bool single_attribute(const Term &rdn) {
    return rdn.is_cons() && rdn.tail_unchecked().is_nil() && rdn.head_unchecked().is_tuple() &&
           rdn.head_unchecked().tuple_size_unchecked() == 3 &&
           atom_is(element(rdn.head_unchecked(), 0), "AttributeTypeAndValue");
}

// One step of is_dir_name/3 over two RDNs: equal, unequal attribute values, or neither clause (the walk stops).
Names::Step Names::rdn_step(const Term &left, const Term &right) {
    if (same(left, right)) {
        return Step::next;
    }
    if (!single_attribute(left) || !single_attribute(right) ||
        !same(element(left.head_unchecked(), 1), element(right.head_unchecked(), 1))) {
        return Step::stop;
    }
    return is_dir_name2(element(left.head_unchecked(), 2), element(right.head_unchecked(), 2)) ? Step::next
                                                                                               : Step::unequal;
}

// is_dir_name/3: two RDN lists name the same entry; without Exact a longer first list may match.
bool Names::is_dir_name(Term first, Term second, const Term &exact) {
    for (; first.is_cons() && second.is_cons(); first = first.tail_unchecked(), second = second.tail_unchecked()) {
        const auto step = rdn_step(first.head_unchecked(), second.head_unchecked());
        if (step == Step::unequal) {
            return false;
        }
        if (step == Step::stop) {
            break;
        }
    }
    if (first.is_nil() && second.is_nil()) {
        return true;
    }
    return second.is_nil() && atom_is(exact, "false");
}

bool Names::is_suffix(const Term &suffix, const Term &text) {
    const auto folded_suffix = casefold(suffix);
    const auto folded = casefold(text);
    const auto items = elements(folded);
    const auto suffix_items = elements(folded_suffix);
    if (!items || !suffix_items) {
        erlang_error(Reason::badarg);
    }
    if (items->size() < suffix_items->size()) {
        return false;
    }
    auto tail = folded;
    for (std::size_t i = 0; i < items->size() - suffix_items->size(); ++i) {
        tail = tail.tail_unchecked();
    }
    return same(tail, folded_suffix);
}

bool is_double_quote(const Term &text) { return is_string(text, "%22") || is_string(text, "\""); }

bool Names::is_double_quoted(const Term &text) {
    if (text.is_cons() && is_string(text.head_unchecked(), "\"")) {
        return is_double_quote(call("lists", "last", {text.tail_unchecked()}));
    }
    if (!starts_with(text, '%') || !text.tail_unchecked().is_cons() ||
        !is_code(text.tail_unchecked().head_unchecked(), '2') ||
        !starts_with(text.tail_unchecked().tail_unchecked(), '2')) {
        return false;
    }
    const auto reversed_tail = reversed(text.tail_unchecked().tail_unchecked().tail_unchecked());
    const auto items = elements(reversed_tail).value_or(std::vector<Term>{});
    if (items.size() < 3) {
        return false;
    }
    const std::array last{items[2], items[1], items[0]};
    return is_double_quote(builder_.list(last));
}

// match_name/4: the rule against each permitted name in turn; an improper rest is function_clause when reached.
bool Names::any(const Named<NameRole> &name, const Named<CandidateRole> &permitted, const Rule rule) {
    auto current = permitted.term.head_unchecked();
    auto rest = permitted.term.tail_unchecked();
    while (true) {
        if (!rest.is_nil() && !rest.is_cons()) {
            no_clause();
        }
        if (rule(*this, name.term, current)) {
            return true;
        }
        if (rest.is_nil()) {
            return false;
        }
        current = rest.head_unchecked();
        rest = rest.tail_unchecked();
    }
}

// The rules of match_name/3's name types.
bool host_or_domain(Names &names, const Term &candidate, const Term &permitted) {
    if (candidate.is_nil()) {
        return false;
    }
    if (starts_with(permitted, '.')) {
        return names.is_suffix(permitted, candidate);
    }
    const auto tokens = names.call("string", "tokens", {candidate, names.chars("@")});
    const auto parts = elements(tokens).value_or(std::vector<Term>{});
    if (parts.size() != 1 && parts.size() != 2) {
        erlang_error(Reason::case_clause, &tokens);
    }
    return names.equal_ignoring_case(parts.back(), permitted);
}

bool email_address(Names &names, const Term &candidate, const Term &permitted) {
    const auto permitted_parts =
        elements(names.call("string", "tokens", {permitted, names.chars("@")})).value_or(std::vector<Term>{});
    if (permitted_parts.size() == 1 && starts_with(permitted, '.')) {
        return names.is_suffix(permitted.tail_unchecked(), candidate);
    }
    if (permitted_parts.size() == 1) {
        const auto tokens = names.call("string", "tokens", {candidate, names.chars("@")});
        const auto parts = elements(tokens).value_or(std::vector<Term>{});
        if (parts.size() != 2) {
            erlang_error(Reason::badmatch, &tokens);
        }
        return names.equal_ignoring_case(parts[1], permitted);
    }
    if (permitted_parts.size() == 2) {
        return names.equal_ignoring_case(candidate, permitted);
    }
    no_clause();
}

bool dns_name(Names &names, const Term &name, const Term &permitted) {
    if (starts_with(permitted, '.') && same(permitted.tail_unchecked(), name)) {
        return true;
    }
    if (starts_with(permitted, '.')) {
        return names.is_suffix(permitted, name);
    }
    const auto length = [&names](const Term &text) { return small(names.call("string", "len", {text}).word()); };
    const auto name_length = length(name);
    const auto permitted_length = length(permitted);
    if (name_length > permitted_length) {
        const auto dot = names.builder().integer('.');
        return names.is_suffix(need(TermFactory(names.builder().context()).cons(dot, permitted)), name);
    }
    return name_length == permitted_length && same(names.casefold(name), names.casefold(permitted));
}

bool or_address(Names &names, const Term &address, const Term &candidate) {
    return names.is_double_quoted(address) && names.is_double_quoted(candidate) &&
           names.equal_ignoring_case(address, candidate);
}

bool rdn_sequence(Names &names, const Term &name, const Term &permitted) {
    if (!tagged(name, "rdnSequence") || !tagged(permitted, "rdnSequence")) {
        no_clause();
    }
    const auto permitted_rdns = element(permitted, 1);
    if (element(name, 1).is_nil() && permitted_rdns.is_cons() && atom_is(permitted_rdns.head_unchecked(), "none") &&
        permitted_rdns.tail_unchecked().is_nil()) {
        return true;
    }
    return names.is_dir_name(element(name, 1), permitted_rdns, names.builder().atom("false"));
}

// is_permitted_ip/3: each byte under its mask equals the permitted one; badarith for a byte that is not an integer.
bool ip_address(Names &names, const Term &address, const Term &permitted) {
    const auto bytes = elements(address);
    const auto allowed = elements(permitted);
    if (!bytes || !allowed || (bytes->size() != 4 && bytes->size() != 16) || allowed->size() != 2 * bytes->size()) {
        return false;
    }
    for (std::size_t i = 0; i < bytes->size(); ++i) {
        const auto masked = names.call("erlang", "band", {(*bytes)[i], (*allowed)[bytes->size() + i]});
        if (!same(masked, (*allowed)[i])) {
            return false;
        }
    }
    return true;
}

// The host of a URI as uri_string:normalize(URI, [return_map]) gives it, or none.
std::optional<Term> uri_host(Names &names, const Term &uri) {
    const auto options = names.builder().atom("return_map");
    const auto normalized = names.call("uri_string", "normalize", {uri, names.builder().list(std::span(&options, 1))});
    if (!normalized.is_map()) {
        return std::nullopt;
    }
    for (const auto &[key, value] : need(normalized.map_entries())) {
        if (atom_is(key, "host")) {
            return value;
        }
    }
    return std::nullopt;
}

bool Names::match_name(const Named<FlagRole> &type_arg, const Named<NameRole> &name,
                       const Named<CandidateRole> &permitted_arg) {
    const auto &type = type_arg.term;
    const auto &permitted = permitted_arg.term;
    if (!permitted.is_cons()) {
        no_clause();
    }
    static const std::array<std::pair<std::string_view, Rule>, 6> RULES{{
        {"rfc822Name", host_or_domain},
        {"directoryName", rdn_sequence},
        {"emailAddress", email_address},
        {"dNSName", dns_name},
        {"x400Address", or_address},
        {"ipAdress", ip_address},
    }};
    for (const auto &[name_type, rule] : RULES) {
        if (atom_is(type, name_type)) {
            return any(name, permitted_arg, rule);
        }
    }
    if (!atom_is(type, "uniformResourceIdentifier")) {
        no_clause();
    }
    // Only the first permitted name is taken as a URI, as OTP does.
    const auto host = uri_host(*this, name.term);
    if (!host) {
        return false;
    }
    const auto first = uri_host(*this, permitted.head_unchecked()).value_or(permitted.head_unchecked());
    const auto rest = permitted.tail_unchecked();
    const auto candidates = need(TermFactory(context_).cons(first, rest));
    return any({*host}, {candidates}, host_or_domain);
}

// to_lower_ascii/1 of pubkey_verify_hostname: ASCII letters lowered through lists and {Type, Value} pairs, IP
// addresses kept.
Term lower(Names &names, const Term &term) {
    if (tagged(term, "ip") || tagged(term, "iPAddress")) {
        return term;
    }
    if (term.is_list()) {
        return names.map(term, [&names](const Term &item) { return lower(names, item); });
    }
    if (term.is_tuple() && term.tuple_size_unchecked() == 2) {
        return names.builder().tuple({element(term, 0), lower(names, element(term, 1))});
    }
    const auto code = small(term.word());
    if (code && *code >= 'A' && *code <= 'Z') {
        return names.builder().integer(*code + ('a' - 'A'));
    }
    return term;
}

// lists:member($*, List).
bool has_wildcard(Term list) {
    for (; list.is_cons(); list = list.tail_unchecked()) {
        if (is_code(list.head_unchecked(), '*')) {
            return true;
        }
    }
    return false;
}

// match_wild_sfx/2: the labels after the wildcard compared from their ends; no other wildcard on either side.
bool wild_suffix(Term name, Term pattern) {
    while (!starts_with(name, '*') && !starts_with(pattern, '*') && name.is_cons() && pattern.is_cons() &&
           same(name.head_unchecked(), pattern.head_unchecked())) {
        name = name.tail_unchecked();
        pattern = pattern.tail_unchecked();
    }
    if (starts_with(name, '*') || starts_with(pattern, '*')) {
        return false;
    }
    return pattern.is_nil() && !has_wildcard(name);
}

// match_wild/2: equal characters up to a wildcard of the pattern.
bool wild(Names &names, Term name, Term pattern) {
    while (true) {
        if (starts_with(pattern, '*')) {
            return wild_suffix(names.reversed(name), names.reversed(pattern.tail_unchecked()));
        }
        if (name.is_cons() && pattern.is_cons() && same(name.head_unchecked(), pattern.head_unchecked())) {
            name = name.tail_unchecked();
            pattern = pattern.tail_unchecked();
            continue;
        }
        return name.is_nil() && pattern.is_nil();
    }
}

// The labels of a host name: [First | Rest] of string:split/2 on ".", a badmatch for anything else.
Term labels(Names &names, const Term &host) {
    const auto split = names.call("string", "split", {lower(names, host), names.chars(".")});
    if (!split.is_cons()) {
        erlang_error(Reason::badmatch, &split);
    }
    return split;
}
} // namespace

Word normalize_name(ProcessContext &context, const Named<NameRole> &name) {
    return recorded_or([&] {
        Names names(context);
        return names.normalize(name.term);
    });
}

Word is_issuer(ProcessContext &context, const Named<NameRole> &issuer, const Named<CandidateRole> &candidate) {
    return recorded_or([&] {
        Names names(context);
        return names.builder().atom(names.is_issuer(issuer.term, candidate.term) ? "true" : "false");
    });
}

Word is_dir_name(ProcessContext &context, const Named<NameRole> &first, const Named<CandidateRole> &second,
                 const Named<FlagRole> &exact) {
    return recorded_or([&] {
        Names names(context);
        return names.builder().atom(names.is_dir_name(first.term, second.term, exact.term) ? "true" : "false");
    });
}

Word match_name(ProcessContext &context, const Named<FlagRole> &type, const Named<NameRole> &name,
                const Named<CandidateRole> &permitted) {
    return recorded_or([&] {
        Names names(context);
        return names.builder().atom(names.match_name(type, name, permitted) ? "true" : "false");
    });
}

Word match_wildcard(ProcessContext &context, const Named<NameRole> &fqdn, const Named<CandidateRole> &name) {
    return recorded_or([&] {
        Names names(context);
        const auto host = labels(names, fqdn.term);
        const auto pattern = labels(names, name.term);
        const auto matched = wild(names, host.head_unchecked(), pattern.head_unchecked()) &&
                             same(host.tail_unchecked(), pattern.tail_unchecked());
        return names.builder().atom(matched ? "true" : "false");
    });
}

Word lower_ascii(ProcessContext &context, const Named<NameRole> &term) {
    return recorded_or([&] {
        Names names(context);
        return lower(names, term.term);
    });
}
} // namespace clause::runtime::builtins::public_key
