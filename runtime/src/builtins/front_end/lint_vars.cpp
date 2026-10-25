#include "lint.hpp"
#include <algorithm>

// erl_lint's variable tables (plan 21 step LIB34C): vectors sorted by variable name standing for OTP's orddicts, with
// vtupdate/2, vtmerge/2, vtnew/2, vtold/2 and the state merges.
namespace clause::runtime::builtins::front_end::lint {
namespace {
// Atom order: by spelling (UTF-8 bytes order as code points).
bool name_less(const Term &left, const Term &right) {
    return left.atom_spelling_unchecked() < right.atom_spelling_unchecked();
}

bool used_either(const VarInfo &left, const VarInfo &right) { return left.used || right.used; }

// orddict:merge/3 of two tables, `both` combining the entries of a name in both.
template <typename Both> VarTable merge(const VarTable &left, const VarTable &right, Both &&both) {
    VarTable out;
    out.reserve(left.size() + right.size());
    auto l = left.begin();
    auto r = right.begin();
    while (l != left.end() && r != right.end()) {
        if (name_less(l->name, r->name)) {
            out.push_back(*l++);
        } else if (name_less(r->name, l->name)) {
            out.push_back(*r++);
        } else {
            out.push_back({.name = l->name, .info = both(l->info, r->info)});
            ++l;
            ++r;
        }
    }
    out.insert(out.end(), l, left.end());
    out.insert(out.end(), r, right.end());
    return out;
}

bool contains(const VarTable &table, const Term &name) { return vtfind(table, name) != nullptr; }
} // namespace

std::vector<Term> merge_annos(const std::vector<Term> &left, const std::vector<Term> &right) {
    std::vector<Term> out;
    std::ranges::set_union(left, right, std::back_inserter(out), TermLess{});
    return out;
}

void merge_state(VarInfo &into, const VarInfo &other) {
    if (into.kind == VarKind::unsafe) {
        return;
    }
    if (other.kind == VarKind::unsafe || into.kind == VarKind::bound) {
        into.kind = other.kind;
        into.where = other.where;
    }
}

VarTable vtupdate(const VarTable &updated, const VarTable &table) {
    return merge(updated, table, [](const VarInfo &left, const VarInfo &right) {
        auto info = left;
        info.used = used_either(left, right);
        info.annos = merge_annos(left.annos, right.annos);
        return info;
    });
}

VarTable vtmerge(const VarTable &left, const VarTable &right) {
    return merge(left, right, [](const VarInfo &first, const VarInfo &second) {
        auto info = first;
        merge_state(info, second);
        info.used = used_either(first, second);
        info.annos = merge_annos(first.annos, second.annos);
        return info;
    });
}

VarTable vtmerge(std::span<const VarTable> tables) {
    VarTable merged;
    for (const auto &table : tables) {
        merged = vtmerge(table, merged);
    }
    return merged;
}

VarTable vtnew(const VarTable &fresh, OldVars old) {
    VarTable out;
    std::ranges::copy_if(fresh, std::back_inserter(out),
                         [&](const VarEntry &entry) { return !contains(old.table, entry.name); });
    return out;
}

VarTable vtold(const VarTable &fresh, OldVars old) {
    VarTable out;
    std::ranges::copy_if(fresh, std::back_inserter(out),
                         [&](const VarEntry &entry) { return contains(old.table, entry.name); });
    return out;
}

VarTable vt_no_unsafe(const VarTable &table) {
    VarTable out;
    std::ranges::copy_if(table, std::back_inserter(out),
                         [](const VarEntry &entry) { return entry.info.kind != VarKind::unsafe; });
    return out;
}

VarTable vt_no_unused(const VarTable &table) {
    VarTable out;
    std::ranges::copy_if(table, std::back_inserter(out), [](const VarEntry &entry) { return entry.info.used; });
    return out;
}

const VarInfo *vtfind(const VarTable &table, const Term &name) {
    const auto found = std::ranges::lower_bound(table, name, name_less, &VarEntry::name);
    return found != table.end() && found->name.word() == name.word() ? &found->info : nullptr;
}

VarTable vt_mark_new(VarKind kind, const Term &where, const VarTable &updated, const VarTable &table) {
    auto out = vtnew(updated, OldVars{table});
    for (auto &entry : out) {
        auto state = var_info(kind, where, false, {});
        merge_state(state, entry.info);
        entry.info.kind = state.kind;
        entry.info.where = state.where;
    }
    return out;
}
} // namespace clause::runtime::builtins::front_end::lint
