#include <array>
#include <clause/abi/frames.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/callable.hpp>

namespace clause::runtime {
namespace {
// Construct the only supported signature without inferring types from term contents.
FunctionKey generic_key(std::string_view name, std::size_t arity) {
    return {std::string(name), arity, std::vector<std::type_index>(arity, typeid(Term))};
}
} // namespace

RegistryResult<void> ModuleRegistry::add(std::string_view name, std::size_t arity, Callable &&target) {
    if (frozen_) {
        return std::unexpected(RegistryError::frozen);
    }
    if (name.empty() || arity > 255 || !target) {
        return std::unexpected(RegistryError::invalid_entry);
    }
    try {
        // Copy the key so Debug STL proxy allocations cannot occur in noexcept string moves.
        const auto key = generic_key(name, arity);
        if (!entries_.try_emplace(key, std::move(target)).second) {
            return std::unexpected(RegistryError::duplicate_key);
        }
        return {};
    } catch (const std::bad_alloc &) {
        return std::unexpected(RegistryError::resource_limit);
    }
}

RegistryResult<const Callable *> ModuleRegistry::find(std::string_view name, std::size_t arity) const {
    if (arity > 255) {
        return std::unexpected(RegistryError::invalid_entry);
    }
    try {
        const auto found = entries_.find(generic_key(name, arity));
        if (found == entries_.end()) {
            return std::unexpected(RegistryError::function_not_found);
        }
        return &found->second;
    } catch (const std::bad_alloc &) {
        return std::unexpected(RegistryError::resource_limit);
    }
}

std::vector<FunctionKey> ModuleRegistry::keys() const {
    std::vector<FunctionKey> result(0);
    result.reserve(entries_.size());
    for (const auto &entry : entries_) {
        result.push_back(entry.first);
    }
    return result;
}

bool ModuleRegistry::frozen() const noexcept { return frozen_; }

void ModuleRegistry::freeze() noexcept { frozen_ = true; }
} // namespace clause::runtime

namespace clause::runtime {
namespace {
// A registrable builtin names its module and function, takes at most 255 arguments and has an implementation.
bool valid(const BuiltinEntry &entry) {
    return !entry.module.empty() && !entry.function.empty() && entry.arity < abi::v1::register_count && entry.body;
}

// The frame calls of a builtin enter: its arity and a null body; it has no name descriptor and no slots.
BuiltinFrame builtin(const BuiltinEntry &entry) {
    return {.frame = {.module = nullptr,
                      .module_atom = 0,
                      .function_atom = 0,
                      .arity = entry.arity,
                      .body = nullptr,
                      .slots = 0,
                      .roots = 0},
            .body = entry.body,
            .quick = entry.quick};
}
} // namespace

RegistryResult<void> BuiltinRegistry::add(std::span<const BuiltinEntry> entries) {
    std::vector<std::map<Key, BuiltinFrame>::iterator> added;
    const auto rollback = [&](RegistryError error) {
        for (const auto &item : added) {
            entries_.erase(item);
        }
        return std::unexpected(error);
    };
    try {
        added.reserve(entries.size());
        for (const auto &entry : entries) {
            if (!valid(entry)) {
                return rollback(RegistryError::invalid_entry);
            }
            const auto [item, fresh] =
                entries_.try_emplace({entry.module, entry.function, entry.arity}, builtin(entry));
            if (!fresh) {
                return rollback(RegistryError::duplicate_key);
            }
            added.push_back(item);
        }
    } catch (const std::bad_alloc &) {
        return rollback(RegistryError::resource_limit);
    }
    for (const auto &item : added) {
        link_bridge(item->first, item->second);
    }
    return {};
}

void BuiltinRegistry::link_bridge(const Key &key, const BuiltinFrame &frame) noexcept {
    const auto &[module, function, arity] = key;
    if (const auto index = abi::v1::find_bridge_builtin(module, function, arity)) {
        bridge_[*index] = &frame;
    }
}

std::string BuiltinRegistry::name_of(const abi::v1::FrameDescriptor &frame) const {
    for (const auto &[key, entry] : entries_) {
        if (&entry.frame == &frame) {
            const auto &[module, function, arity] = key;
            return std::string(module) + ":" + std::string(function) + "/" + std::to_string(arity);
        }
    }
    return {};
}

const BuiltinFrame *BuiltinRegistry::find(std::string_view module, std::string_view function,
                                          std::size_t arity) const noexcept {
    const auto found = entries_.find({module, function, arity});
    return found == entries_.end() ? nullptr : &found->second;
}

const BuiltinFrame *BuiltinRegistry::bridge(std::size_t index) const noexcept {
    return index < bridge_.size() ? bridge_[index] : nullptr;
}

std::vector<std::string_view> BuiltinRegistry::modules() const {
    std::vector<std::string_view> result;
    for (const auto &[key, frame] : entries_) {
        if (result.empty() || result.back() != std::get<0>(key)) {
            result.push_back(std::get<0>(key));
        }
    }
    return result;
}

// Every module's families, in catalog order; each family is one translation unit (docs/builtins.md#source-layout).
std::span<const std::span<const BuiltinEntry>> production_builtins() noexcept {
    static const std::array families{erlang_builtins(),
                                     term_access_builtins(),
                                     list_builtins(),
                                     conversion_builtins(),
                                     io_builtins(),
                                     process_builtins(),
                                     port_builtins(),
                                     os_builtins(),
                                     dictionary_builtins(),
                                     binary_conversion_builtins(),
                                     term_encoding_builtins(),
                                     term_decoding_builtins(),
                                     time_builtins(),
                                     timer_builtins(),
                                     introspection_builtins(),
                                     system_builtins(),
                                     spawning_builtins(),
                                     fun_info_builtins(),
                                     apply_builtins(),
                                     hashing_builtins(),
                                     md5_builtins(),
                                     node_builtins(),
                                     code_builtins(),
                                     lists_sublist_builtins(),
                                     lists_ordering_builtins(),
                                     lists_key_builtins(),
                                     lists_merge_builtins(),
                                     lists_zip_builtins(),
                                     math_builtins(),
                                     binary_search_builtins(),
                                     binary_part_builtins(),
                                     binary_encoding_builtins(),
                                     maps_builtins(),
                                     ordsets_builtins(),
                                     orddict_builtins(),
                                     proplists_builtins(),
                                     dict_builtins(),
                                     sets_builtins(),
                                     gb_trees_builtins(),
                                     gb_sets_builtins(),
                                     queue_builtins(),
                                     array_builtins(),
                                     calendar_builtins(),
                                     timer_module_builtins(),
                                     base64_builtins(),
                                     json_builtins(),
                                     filename_builtins(),
                                     filename_join_builtins(),
                                     uri_parse_builtins(),
                                     uri_compose_builtins(),
                                     uri_codec_builtins(),
                                     string_text_builtins(),
                                     string_search_builtins(),
                                     string_lexeme_builtins(),
                                     string_number_builtins(),
                                     string_latin1_builtins(),
                                     json_encode_builtins(),
                                     rand_builtins(),
                                     persistent_term_builtins(),
                                     unicode_builtins(),
                                     code_module_builtins(),
                                     init_builtins(),
                                     atomics_builtins(),
                                     erts_internal_builtins(),
                                     unicode_table_builtins(),
                                     unicode_form_builtins(),
                                     io_lib_format_builtins(),
                                     io_lib_write_builtins(),
                                     io_lib_collect_builtins(),
                                     io_lib_fread_builtins(),
                                     io_lib_order_builtins(),
                                     erl_anno_builtins(),
                                     erl_internal_builtins(),
                                     erl_scan_builtins(),
                                     erl_parse_builtins(),
                                     erl_eval_builtins(),
                                     expand_records_builtins(),
                                     lint_builtins(),
                                     erl_pp_builtins(),
                                     epp_builtins(),
                                     erl_features_builtins(),
                                     sofs_callbacks_builtins(),
                                     sofs_constructors_builtins(),
                                     sofs_families_builtins(),
                                     sofs_products_builtins(),
                                     sofs_relations_builtins(),
                                     sofs_selectors_builtins(),
                                     sofs_sets_builtins(),
                                     graph_query_builtins(),
                                     graph_traversal_builtins(),
                                     ets_table_builtins(),
                                     ets_info_builtins(),
                                     ets_write_builtins(),
                                     ets_read_builtins(),
                                     ets_walk_builtins(),
                                     ets_update_builtins(),
                                     ets_select_builtins(),
                                     ets_select_edit_builtins(),
                                     ets_match_spec_builtins(),
                                     digraph_builtins(),
                                     disk_log_builtins(),
                                     dets_builtins(),
                                     checksum_builtins(),
                                     tuple_edit_builtins(),
                                     process_option_builtins(),
                                     system_flag_builtins(),
                                     packet_builtins(),
                                     suspension_builtins(),
                                     records_builtins(),
                                     re_builtins(),
                                     zlib_builtins(),
                                     re_pattern_builtins(),
                                     re_matching_builtins(),
                                     receive_builtins()};
    return families;
}
} // namespace clause::runtime
