#pragma once
#include "uri.hpp"
#include <optional>

// The components parse/1 finds (plan 21 step LIB5G, docs/builtins.md#uri-string).
namespace clause::runtime::builtins::uri {
struct Uri {
    std::optional<Term> scheme;
    std::optional<Term> userinfo;
    std::optional<Term> host;
    // An integer, or the atom undefined for an empty port.
    std::optional<Term> port;
    std::optional<Term> path;
    std::optional<Term> query;
    std::optional<Term> fragment;

    // The map of the components; with `lists`, binaries become lists of characters.
    Term term(ProcessContext &context, bool lists) const;
};
} // namespace clause::runtime::builtins::uri

namespace clause::runtime::builtins::uri {
// A map's entries, read and changed by atom key (maps:get/put/remove and maps:merge/with as uri_string uses them).
class Fields final {
  public:
    Fields(ProcessContext &context, const Term &map);

    std::optional<Term> get(std::string_view key) const;

    bool has(std::string_view key) const { return get(key).has_value(); }

    void set(AtomName key, const Term &value);
    void erase(std::string_view key);
    // maps:merge(This, maps:with(Keys, Other)).
    void merge_from(const Fields &other, std::initializer_list<std::string_view> keys);
    Term term() const;

    const std::vector<std::pair<Term, Term>> &entries() const { return entries_; }

    std::vector<std::pair<Term, Term>> &entries() { return entries_; }

  private:
    std::ptrdiff_t find(std::string_view key) const;

    ProcessContext &context_;
    std::vector<std::pair<Term, Term>> entries_;
};

// parse/1: the map, or its {error, invalid_uri, Fragment} answer.
Term parse(ProcessContext &context, const Term &string);
// recompose/1 of a map, or its {error, ...} answer.
Term recompose(ProcessContext &context, const Term &map);
// RFC 3986's remove_dot_segments of a path as uri_string applies it (lists through UTF-8).
Term without_dots(ProcessContext &context, const Term &path);
} // namespace clause::runtime::builtins::uri
