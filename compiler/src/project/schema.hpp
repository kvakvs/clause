#pragma once
#include "diagnostics.hpp"
#include "loader.hpp"
#include <span>

namespace clause::project::schema {
struct Context {
    // Associate every decoded value with the owning manifest and target.
    std::filesystem::path file;
    std::string target;
};

// Copy a node's source position into owned project diagnostic context.
Site site(const toml::node &node, const Context &context, std::string key);
// Reject unrecognized fields rather than silently ignoring configuration typos.
void keys(const toml::table &table, std::initializer_list<std::string_view> allowed, const Context &context);
// Require the exact schema type and preserve nonempty string spelling.
Text text(const toml::node &node, const Context &context, std::string key);
// Decode an optional ordered string array without coercing element types.
std::vector<Text> strings(const toml::table &table, std::string_view key, const Context &context);
// Decode an optional boolean; absent means false.
bool boolean(const toml::table &table, std::string_view key, const Context &context);
// Require a table at a known schema boundary.
const toml::table &table(const toml::node &node, const Context &context, std::string key);
// Bound total decoded input entries before allocating the manifest model.
void budget(const toml::node &node, std::size_t &remaining, const Context &context);
} // namespace clause::project::schema
