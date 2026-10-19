#include "disposal.hpp"
#include "../portions.hpp"
#include <algorithm>
#include <cstddef>

// Freeing the objects of deleted or emptied tables in portions (plan 21 step LIB10F): each portion frees as many
// objects as the process's budget allows, then the process traps until a later slice.
namespace clause::runtime::builtins::ets {
namespace {
// The contents still to free between portions.
struct Disposal final : TrapState {
    Table::Contents contents;
};

Word dispose_continue(ProcessContext &context, Arguments);

constexpr BuiltinFrame DISPOSE_FRAME = continuation_frame(&guarded<dispose_continue>, 0);

// Budget units freeing one object takes, as a walk's visit of it does.
constexpr std::size_t OBJECT_WORK = 4;

// Free contents within the budget; whether everything is freed.
bool shed(ProcessContext &context, Table::Contents &contents) {
    auto &stack = context.stack();
    const auto objects = std::max<std::size_t>(stack.budget() / OBJECT_WORK, 1);
    auto left = objects;
    while (!contents.empty() && left > 0) {
        if (contents.back()->shed(left)) {
            contents.pop_back();
        }
    }
    stack.spend((objects - left) * OBJECT_WORK);
    return contents.empty();
}

Word finished(ProcessContext &context) {
    context.stack().drop_trap_state();
    return atom(context, "true"_atom).word();
}

Word dispose_continue(ProcessContext &context, Arguments) {
    auto &state = trap_state<Disposal>(context);
    if (shed(context, state.contents)) {
        return finished(context);
    }
    context.stack().trap(DISPOSE_FRAME.frame, {});
    return 0;
}
} // namespace

Word dispose(ProcessContext &context, Table::Contents contents) {
    if (shed(context, contents)) {
        return atom(context, "true"_atom).word();
    }
    auto state = std::make_unique<Disposal>();
    state->contents = std::move(contents);
    context.stack().keep_trap_state(std::move(state));
    context.stack().trap(DISPOSE_FRAME.frame, {});
    return 0;
}
} // namespace clause::runtime::builtins::ets
