#include "../../terms/integers.hpp"
#include "../portions.hpp"
#include "list_walk.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The lists builtins that combine lists element by element (lists zips, docs/builtins.md#lists-module): zip/2,3,
// zip3/3,4, unzip/1, unzip3/1 and enumerate/1,2,3. Lists of different lengths fail, are trimmed or are padded, as the
// How argument says; an argument no OTP clause accepts raises function_clause.
namespace clause::runtime::builtins::lists {
namespace {
// How zip/3 and zip3/4 treat lists of different lengths.
struct Policy {
    enum class Kind : std::uint8_t { fail, trim, pad };
    Kind kind = Kind::fail;
    // The element each list contributes once it has ended, for pad.
    std::vector<Term> defaults;
};

// Whether `term` is the atom `name`.
bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The defaults of {pad, Defaults} with one default per list; none for any other term.
std::optional<std::vector<Term>> pad_defaults(const Term &how, std::size_t lists) {
    if (!how.is_tuple() || how.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto parts = need(how.tuple_elements());
    if (!is_atom(parts[0], "pad") || !parts[1].is_tuple() || parts[1].tuple_size_unchecked() != lists) {
        return std::nullopt;
    }
    return need(parts[1].tuple_elements());
}

// The How argument for `lists` lists: fail, trim or {pad, Defaults}.
Policy policy(const Term &how, std::size_t lists) {
    if (is_atom(how, "fail") || is_atom(how, "trim")) {
        return {is_atom(how, "fail") ? Policy::Kind::fail : Policy::Kind::trim, {}};
    }
    auto defaults = pad_defaults(how, lists);
    if (!defaults) {
        no_clause();
    }
    return {Policy::Kind::pad, std::move(*defaults)};
}

// Whether the zip of `rests` is complete under `policy`: every list ended, or trim with every one still a list;
// function_clause for lengths the policy refuses and for improper lists.
bool zip_done(std::span<const Term> rests, const Policy &policy) {
    const auto ended = std::ranges::count_if(rests, [](const Term &rest) { return rest.is_nil(); });
    const bool lists = std::ranges::all_of(rests, [](const Term &rest) { return rest.is_list(); });
    if (static_cast<std::size_t>(ended) == rests.size() || (policy.kind == Policy::Kind::trim && lists)) {
        return true;
    }
    if (policy.kind != Policy::Kind::pad || !lists) {
        no_clause();
    }
    return false;
}

// The tuple of the next position: each list's element, advancing it, or its default once it ended.
Term next_tuple(TermFactory &factory, std::vector<Term> &rests, const Policy &policy) {
    std::vector<Term> row;
    row.reserve(rests.size());
    for (std::size_t i = 0; i < rests.size(); ++i) {
        const bool more = rests[i].is_cons();
        row.push_back(more ? rests[i].head_unchecked() : policy.defaults[i]);
        rests[i] = more ? rests[i].tail_unchecked() : rests[i];
    }
    return need(factory.tuple(row));
}

// The tuples of zip/3 or zip3/4, one per position.
TermResult<Term> zip_lists(ProcessContext &context, std::vector<Term> rests, const Policy &policy) {
    TermFactory factory(context);
    std::vector<Term> tuples;
    while (std::ranges::all_of(rests, [](const Term &rest) { return rest.is_cons(); }) || !zip_done(rests, policy)) {
        tuples.push_back(next_tuple(factory, rests, policy));
    }
    return factory.list(tuples);
}

// zip(List1, List2, How) and zip(List1, List2).
TermResult<Term> zip_how(ProcessContext &context, const Term &first, const Term &second, const Term &how) {
    return zip_lists(context, {first, second}, policy(how, 2));
}

TermResult<Term> zip(ProcessContext &context, const Term &first, const Term &second) {
    return zip_lists(context, {first, second}, Policy{});
}

// zip3(List1, List2, List3, How) and zip3(List1, List2, List3).
TermResult<Term> zip3_how(ProcessContext &context, const Term &first, const Term &second, const Term &third,
                          const Term &how) {
    return zip_lists(context, {first, second, third}, policy(how, 3));
}

TermResult<Term> zip3(ProcessContext &context, const Term &first, const Term &second, const Term &third) {
    return zip_lists(context, {first, second, third}, Policy{});
}

// The lists of the elements of a proper list of `arity`-tuples, one list per position, as a tuple.
TermResult<Term> unzip_tuples(ProcessContext &context, const Term &list, std::size_t arity) {
    std::vector<std::vector<Term>> columns(arity);
    for (const auto &element : proper(list)) {
        if (!element.is_tuple() || element.tuple_size_unchecked() != arity) {
            no_clause();
        }
        const auto fields = need(element.tuple_elements());
        for (std::size_t i = 0; i < arity; ++i) {
            columns[i].push_back(fields[i]);
        }
    }
    TermFactory factory(context);
    std::vector<Term> lists;
    lists.reserve(arity);
    for (const auto &column : columns) {
        lists.push_back(need(factory.list(column)));
    }
    return factory.tuple(lists);
}

// unzip(List1) and unzip3(List1).
TermResult<Term> unzip(ProcessContext &context, const Term &list) { return unzip_tuples(context, list, 2); }

TermResult<Term> unzip3(ProcessContext &context, const Term &list) { return unzip_tuples(context, list, 3); }

struct Numbering {
    // The Index and Step of enumerate/3: the first number and the difference between consecutive ones.
    Term index;
    Term step;
};

// {Index + K * Step, Element} for the K-th element (from 0) of a proper list; Index and Step are any integers.
Word enumerate(ProcessContext &context, const Numbering &numbering, const Term &list) {
    if (!numbering.index.is_integer() || !numbering.step.is_integer()) {
        no_clause();
    }
    auto position = need(detail::integer_read(numbering.index));
    const auto stride = need(detail::integer_read(numbering.step));
    TermFactory factory(context);
    std::vector<Term> pairs;
    for (const auto &element : proper(list)) {
        const std::array pair{need(detail::IntegerAccess::make(context.heap(), position)), element};
        pairs.push_back(need(factory.tuple(pair)));
        position += stride;
    }
    return need(factory.list(pairs)).word();
}

// enumerate(Index, Step, List1), enumerate(Index, List1) (a step of 1) and enumerate(List1) (from 1).
Word enumerate3(ProcessContext &context, Arguments arguments) {
    return enumerate(context, {term_of(context, arguments[0]), term_of(context, arguments[1])},
                     term_of(context, arguments[2]));
}

Word enumerate2(ProcessContext &context, Arguments arguments) {
    const auto one = need(TermFactory(context).integer(1));
    return enumerate(context, {term_of(context, arguments[0]), one}, term_of(context, arguments[1]));
}

Word enumerate1(ProcessContext &context, Arguments arguments) {
    const auto one = need(TermFactory(context).integer(1));
    return enumerate(context, {one, one}, term_of(context, arguments[0]));
}

constexpr std::array ZIP_BUILTINS{
    typed_entry<zip>("lists", "zip"),
    typed_entry<zip_how>("lists", "zip"),
    typed_entry<zip3>("lists", "zip3"),
    typed_entry<zip3_how>("lists", "zip3"),
    typed_entry<unzip>("lists", "unzip"),
    typed_entry<unzip3>("lists", "unzip3"),
    BuiltinEntry{"lists", "enumerate", 1, &guarded<enumerate1>},
    BuiltinEntry{"lists", "enumerate", 2, &guarded<enumerate2>},
    BuiltinEntry{"lists", "enumerate", 3, &guarded<enumerate3>},
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_zip_builtins() noexcept { return builtins::lists::ZIP_BUILTINS; }
} // namespace clause::runtime
