#include "schema.hpp"
#include <algorithm>

namespace clause::project::schema {
Site site(const toml::node &node, const Context &context, std::string key) {
    return {context.file, std::move(key), context.target, node.source().begin.line, node.source().begin.column};
}

void keys(const toml::table &table, const std::initializer_list<std::string_view> allowed, const Context &context) {
    for (const auto &[key, node] : table) {
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end()) {
            fail(site(node, context, std::string(key.str())), "unknown key");
        }
    }
}

Text text(const toml::node &node, const Context &context, std::string key) {
    const auto where = site(node, context, std::move(key));
    const auto value = node.value<std::string>();
    if (!value || value->empty()) {
        fail(where, "expected a nonempty string");
    }
    if (value->find('\0') != std::string::npos) {
        fail(where, "embedded NUL is not allowed");
    }
    return {*value, where};
}

std::vector<Text> strings(const toml::table &table, const std::string_view key, const Context &context) {
    const auto *node = table.get(key);
    if (!node) {
        return {};
    }
    const auto *array = node->as_array();
    if (!array) {
        fail(site(*node, context, std::string(key)), "expected an array of strings");
    }
    std::vector<Text> result;
    result.reserve(array->size());
    for (const auto &element : *array) {
        result.push_back(text(element, context, std::string(key)));
    }
    return result;
}

bool boolean(const toml::table &table, const std::string_view key, const Context &context) {
    const auto *node = table.get(key);
    if (!node) {
        return false;
    }
    const auto value = node->value<bool>();
    if (!node->is_boolean() || !value) {
        fail(site(*node, context, std::string(key)), "expected a boolean");
    }
    return *value;
}

const toml::table &table(const toml::node &node, const Context &context, std::string key) {
    const auto *result = node.as_table();
    if (!result) {
        fail(site(node, context, std::move(key)), "expected a table");
    }
    return *result;
}

void budget(const toml::node &node, std::size_t &remaining, const Context &context) {
    if (remaining == 0) {
        fail(site(node, context, {}), "configuration entry limit exceeded");
    }
    --remaining;
    if (const auto *values = node.as_array()) {
        for (const auto &value : *values) {
            budget(value, remaining, context);
        }
    }
    if (const auto *values = node.as_table()) {
        for (const auto &[key, value] : *values) {
            (void)key;
            budget(value, remaining, context);
        }
    }
}
} // namespace clause::project::schema
