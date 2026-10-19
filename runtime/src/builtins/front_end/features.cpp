#include "../string/string.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>

// erl_features (plan 21 step LIB8B, docs/builtins.md#compiler-tables): the features of OTP 29 as Clause's compiler
// knows them (maybe_expr approved and enabled, compr_assign experimental), the features each module was compiled
// with, and clause_features:beam_meta/1, which finds the Meta chunk of a .beam file for erl_features:used/1.
namespace clause::runtime::builtins::front_end {
namespace {
using string::Terms;

// One feature as erl_features:info/1 describes it.
struct Feature {
    std::string_view name;
    std::string_view short_text;
    std::string_view description;
    std::string_view status;
    std::int64_t experimental;
    // The release that approved it; 0 while experimental.
    std::int64_t approved;
    std::array<std::string_view, 2> keywords;
    // Whether a program has it enabled unless an option changes that.
    bool enabled;
};

constexpr std::array<Feature, 2> FEATURES{{
    {"compr_assign",
     "Assignment in comprehensions (EEP77)",
     "Implementation of '=' assignments in comprehensions proposed in EEP77.",
     "experimental",
     29,
     0,
     {},
     false},
    {"maybe_expr",
     "Value based error handling (EEP49)",
     "Implementation of the maybe expression proposed in EEP49 -- Value based error handling.",
     "approved",
     25,
     27,
     {"maybe", "else"},
     true},
}};

const Feature *feature_of(const Term &name) {
    if (!name.is_atom()) {
        return nullptr;
    }
    const auto found = std::ranges::find(FEATURES, name.atom_spelling_unchecked(), &Feature::name);
    return found == FEATURES.end() ? nullptr : &*found;
}

// error(invalid_feature) for a name that is not a feature.
const Feature &valid(ProcessContext &context, const Term &name) {
    const auto *feature = feature_of(name);
    if (feature == nullptr) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = Terms(context).atom("invalid_feature"_atom).word()};
    }
    return *feature;
}

Term keyword_list(const Terms &terms, const Feature &feature) {
    std::vector<Term> words;
    for (const auto keyword : feature.keywords) {
        if (!keyword.empty()) {
            words.push_back(terms.atom(keyword));
        }
    }
    return terms.list(words);
}

// all/0 and configurable/0: every feature, sorted (none is permanent or rejected in OTP 29).
Term all(ProcessContext &context) {
    const Terms terms(context);
    std::vector<Term> names;
    names.reserve(FEATURES.size());
    for (const auto &feature : FEATURES) {
        names.push_back(terms.atom(feature.name));
    }
    return terms.list(names);
}

// enabled/0: the features a program runs with.
Term enabled(ProcessContext &context) {
    const Terms terms(context);
    std::vector<Term> names;
    for (const auto &feature : FEATURES) {
        if (feature.enabled) {
            names.push_back(terms.atom(feature.name));
        }
    }
    return terms.list(names);
}

// keywords/0: the keywords of the enabled features.
Term keywords0(ProcessContext &context) {
    const Terms terms(context);
    std::vector<Term> words;
    for (const auto &feature : FEATURES) {
        for (const auto keyword : feature.keywords) {
            if (feature.enabled && !keyword.empty()) {
                words.push_back(terms.atom(keyword));
            }
        }
    }
    return terms.list(words);
}

Term keywords1(ProcessContext &context, const Term &name) { return keyword_list(Terms(context), valid(context, name)); }

// info(Feature): its description map.
Term info(ProcessContext &context, const Term &name) {
    const auto &feature = valid(context, name);
    const Terms terms(context);
    const auto text = [&](std::string_view value) {
        std::u32string codes(value.begin(), value.end());
        std::vector<Term> items;
        for (const auto code : codes) {
            items.push_back(terms.integer(code));
        }
        return terms.list(items);
    };
    std::vector<std::pair<Term, Term>> entries{
        {terms.atom("short"_atom), text(feature.short_text)},
        {terms.atom("description"_atom), text(feature.description)},
        {terms.atom("status"_atom), terms.atom(feature.status)},
        {terms.atom("experimental"_atom), terms.integer(feature.experimental)},
        {terms.atom("keywords"_atom), keyword_list(terms, feature)},
        {terms.atom("type"_atom), terms.atom("extension"_atom)},
    };
    if (feature.approved != 0) {
        entries.emplace_back(terms.atom("approved"_atom), terms.integer(feature.approved));
    }
    return need(TermFactory(context).map(entries));
}

// clause_features:module_features(Module): the features a module of the program was compiled with, or not_found.
Term module_features(ProcessContext &context, const Term &module) {
    const Terms terms(context);
    const auto features = module.is_atom() ? context.code_server().module_features(module.word()) : std::nullopt;
    if (!features) {
        return terms.atom("not_found"_atom);
    }
    std::vector<Term> names;
    names.reserve(features->size());
    std::ranges::transform(*features, std::back_inserter(names),
                           [&](Word word) { return need(Term::from_word(word, context)); });
    return terms.list(names);
}

std::uint32_t big_endian(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + i]);
    }
    return value;
}

// clause_features:beam_meta(Binary): the Meta chunk of a BEAM file's bytes, missing when it has none, error when the
// bytes are not a BEAM file.
Term beam_meta(ProcessContext &context, const Term &binary) {
    const Terms terms(context);
    if (!binary.is_binary()) {
        return terms.atom("error"_atom);
    }
    const auto bytes = need(binary.binary_bytes());
    const auto tag = [&](std::size_t at, std::string_view name) {
        return at + 4 <= bytes.size() &&
               std::equal(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at),
                          [](char c, std::byte b) { return static_cast<std::byte>(c) == b; });
    };
    if (!tag(0, "FOR1") || !tag(8, "BEAM")) {
        return terms.atom("error"_atom);
    }
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto size = big_endian(bytes, at + 4);
        if (at + 8 + size > bytes.size()) {
            return terms.atom("error"_atom);
        }
        if (tag(at, "Meta")) {
            return terms.binary(std::span(bytes).subspan(at + 8, size));
        }
        at += 8 + ((size + 3U) & ~3U);
    }
    return terms.atom("missing"_atom);
}

constexpr std::array FEATURES_BUILTINS{
    typed_entry<all>("erl_features", "all"),
    typed_entry<all>("erl_features", "configurable"),
    typed_entry<enabled>("erl_features", "enabled"),
    typed_entry<info>("erl_features", "info"),
    typed_entry<keywords0>("erl_features", "keywords"),
    typed_entry<keywords1>("erl_features", "keywords"),
    typed_entry<module_features>("clause_features", "module_features"),
    typed_entry<beam_meta>("clause_features", "beam_meta"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_features_builtins() noexcept { return builtins::front_end::FEATURES_BUILTINS; }
} // namespace clause::runtime
