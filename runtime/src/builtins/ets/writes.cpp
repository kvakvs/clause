#include "ets.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The ets write builtins (plan 21 steps LIB10A, LIB10C, docs/builtins.md#ets): insert/2, insert_new/2, delete/2,
// delete_object/2, delete_all_objects/1 and take/2. A list of objects is stored with the table locked alone, so it
// is atomic and isolated as in OTP.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::Index;

Term yes(ProcessContext &context) { return atom(context, "true"); }

// The key of an object argument, checked as insert/2 checks it, without copying the object.
Key object_key(ProcessContext &context, const Table &table, const Term &object) {
    const auto keypos = table.options().keypos;
    if (!object.is_tuple() || object.tuple_size_unchecked() < keypos) {
        bad_argument();
    }
    return key(context, table, object.tuple_element_unchecked(keypos - 1));
}

// insert(Table, ObjectOrObjects).
Term insert(ProcessContext &context, const TableArgument &argument, const Term &objects) {
    const auto found = table(context, argument, Access::write);
    auto items = stored_objects(context, *found, objects);
    if (items.size() == 1) {
        auto &item = items.front();
        need_table(found->write(item.key, [&item](Index &index) { index.insert(item.key, std::move(item.object)); }));
    } else if (!items.empty()) {
        need_table(found->exclusive([&items](Table &locked) {
            for (auto &item : items) {
                locked.index(item.key).insert(item.key, std::move(item.object));
            }
        }));
    }
    return yes(context);
}

// Store one object unless its key is in the table; whether it was stored.
bool insert_one_new(Table &table, Stored &item) {
    auto inserted = false;
    need_table(table.write(item.key, [&item, &inserted](Index &index) {
        inserted = index.find(item.key) == nullptr;
        if (inserted) {
            index.insert(item.key, std::move(item.object));
        }
    }));
    return inserted;
}

// Store every object unless any key is in the table, with the table locked alone; whether they were stored.
bool insert_all_new(Table &table, std::vector<Stored> &items) {
    auto inserted = false;
    need_table(table.exclusive([&items, &inserted](Table &locked) {
        inserted = std::ranges::none_of(
            items, [&locked](const Stored &item) { return locked.index(item.key).find(item.key) != nullptr; });
        if (!inserted) {
            return;
        }
        for (auto &item : items) {
            locked.index(item.key).insert(item.key, std::move(item.object));
        }
    }));
    return inserted;
}

// insert_new(Table, ObjectOrObjects): false, storing nothing, when any key is in the table already.
Term insert_new(ProcessContext &context, const TableArgument &argument, const Term &objects) {
    const auto found = table(context, argument, Access::write);
    auto items = stored_objects(context, *found, objects);
    const auto inserted =
        items.empty() || (items.size() == 1 ? insert_one_new(*found, items.front()) : insert_all_new(*found, items));
    return need(TermFactory(context).boolean(inserted));
}

// delete(Table, Key).
Term delete_key(ProcessContext &context, const TableArgument &argument, const Term &term) {
    const auto found = table(context, argument, Access::write);
    const auto at = key(context, *found, term);
    need_table(found->write(at, [&at](Index &index) { static_cast<void>(index.erase(at)); }));
    return yes(context);
}

// delete_object(Table, Object): only objects exactly equal to Object go.
Term delete_object(ProcessContext &context, const TableArgument &argument, const Term &object) {
    const auto found = table(context, argument, Access::write);
    const auto at = object_key(context, *found, object);
    need_table(found->write(at, [&at, &object](Index &index) { index.erase_object(at, object.word()); }));
    return yes(context);
}

// delete_all_objects(Table).
Term delete_all_objects(ProcessContext &context, const TableArgument &argument) {
    const auto found = table(context, argument, Access::write);
    need_table(found->exclusive([](Table &locked) {
        for (std::size_t part = 0; part < locked.part_count(); ++part) {
            locked.part(part).clear();
        }
    }));
    return yes(context);
}

// take(Table, Key): the objects of Key, removed.
Term take(ProcessContext &context, const TableArgument &argument, const Term &term) {
    const auto found = table(context, argument, Access::write);
    const auto at = key(context, *found, term);
    auto taken = need(TermFactory(context).nil());
    need_table(found->write(at, [&](Index &index) {
        if (const auto *objects = index.find(at)) {
            taken = objects_list(context, *objects);
            static_cast<void>(index.erase(at));
        }
    }));
    return taken;
}

constexpr std::array ETS_WRITE_BUILTINS{
    typed_entry<insert>("ets", "insert"),
    typed_entry<insert_new>("ets", "insert_new"),
    typed_entry<delete_key>("ets", "delete"),
    typed_entry<delete_object>("ets", "delete_object"),
    typed_entry<delete_all_objects>("ets", "delete_all_objects"),
    typed_entry<take>("ets", "take"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_write_builtins() noexcept { return builtins::ets::ETS_WRITE_BUILTINS; }
} // namespace clause::runtime
