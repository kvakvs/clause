#include "module_registration.hpp"
#include "../semantic/record_definitions.hpp"
#include "../semantic/records.hpp"
#include "../semantic/services.hpp"
#include "../semantic/symbols.hpp"
#include "code_convention.hpp"
#include "module_atoms.hpp"
#include "runtime_symbols.hpp"
#include <clause/abi/v1.hpp>
#include <clause/compiler/source.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

namespace clause::codegen {
namespace {
// Retain exact UTF-8 bytes, including embedded NULs, using explicit lengths in every descriptor.
llvm::Constant *spelling(llvm::Module &output, const std::string &name) {
    auto *bytes = llvm::ConstantDataArray::getString(output.getContext(), name, false);
    return new llvm::GlobalVariable(output, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage, bytes,
                                    "module.spelling");
}

// The `<symbol>.frame` FrameDescriptor of a function of this module, which lower_frames defines.
llvm::Constant *frame_of(llvm::Module &output, const std::string &symbol, llvm::IntegerType *word) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    auto *frame = llvm::StructType::get(output.getContext(), {ptr, word, word, word, ptr, word, word});
    return output.getOrInsertGlobal(symbol + ".frame", frame);
}

// Store only declared exports, each with its host entry and the frame dynamic calls enter; private implementations
// remain reachable solely through direct calls. An export the executable cannot reach keeps only its name (62C).
llvm::Constant *exports(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                        std::size_t &count) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    auto *type = llvm::StructType::get(ptr, word, word, ptr, ptr);
    auto *none = llvm::ConstantPointerNull::get(ptr);
    std::vector<llvm::Constant *> entries;
    for (const auto &function : module.functions) {
        if (!function.exported) {
            continue;
        }
        const auto name = utf8(function.key.name);
        auto *entry = function.live_ ? static_cast<llvm::Constant *>(output.getFunction(function.symbol)) : none;
        auto *frame = function.live_ ? frame_of(output, function.symbol, word) : none;
        entries.push_back(llvm::ConstantStruct::get(type, spelling(output, name),
                                                    llvm::ConstantInt::get(word, name.size()),
                                                    llvm::ConstantInt::get(word, function.key.arity), entry, frame));
    }
    count = entries.size();
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(type, count), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "module.exports");
}

// One abi::v1::RecordDescriptor per native record, in semantic::native_layouts order, naming atom slots of this
// module; lowering addresses entries of this `<prefix>.records` table.
llvm::GlobalVariable *records(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                              llvm::GlobalVariable *descriptor, const std::string &prefix) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    auto *type = llvm::StructType::get(ptr, word, word, word, ptr, word, ptr, word);
    const auto slot = [&](const ast::Atom &atom) { return atom_slot(output, utf8(atom.name)); };
    const auto layouts = semantic::native_layouts(module);
    std::vector<llvm::Constant *> entries;
    entries.reserve(layouts.size());
    for (const auto *layout : layouts) {
        std::vector<llvm::Constant *> slots;
        slots.reserve(layout->fields.size());
        for (const auto &field : layout->fields) {
            slots.push_back(slot(field.name));
        }
        auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(word, slots.size()), slots);
        auto *fields = new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage,
                                                array, "record.fields");
        const bool exported = module.exported_records.contains(layout->name.name);
        // Added for records:get_definition/2: the field list with folded defaults as term_to_binary bytes.
        const auto definition = semantic::record_definition_etf(*module.syntax, *layout);
        entries.push_back(llvm::ConstantStruct::get(
            type, descriptor, slot(ast::Atom{module.name}), slot(layout->name),
            llvm::ConstantInt::get(word, exported ? 1 : 0), fields, llvm::ConstantInt::get(word, slots.size()),
            spelling(output, definition), llvm::ConstantInt::get(word, definition.size())));
    }
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(type, entries.size()), entries);
    // Other modules of the batch construct exported records from this table.
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::ExternalLinkage, array,
                                    prefix + ".records");
}

// Added for erl_features:used/1: the atom slots of the features enabled when the module was compiled.
llvm::Constant *features(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                         std::size_t &count) {
    const auto snapshot = module.syntax->features();
    std::vector<llvm::Constant *> slots;
    for (const auto &name : snapshot ? snapshot->enabled : std::vector<std::string>{}) {
        slots.push_back(atom_slot(output, name));
    }
    count = slots.size();
    if (slots.empty()) {
        return llvm::ConstantPointerNull::get(llvm::PointerType::get(output.getContext(), 0));
    }
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(word, slots.size()), slots);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "module.features");
}

// The exported function of a batch module an external fun names; null when the batch exports none or a builtin has
// the name.
const semantic::Function *exported(const semantic::Module &module, const semantic::FunEntry &fun) {
    // Builtins are sticky: fun erlang:abs/1 binds the runtime's builtin, never a library stub (step 41B).
    const auto peer = module.peers.find(fun.module);
    if (peer == module.peers.end() || semantic::bridge_builtin(fun.module, {fun.function, fun.arity})) {
        return nullptr;
    }
    const auto &owner = *peer->second;
    const auto found = owner.lookup.find({fun.function, fun.arity});
    if (found == owner.lookup.end()) {
        return nullptr;
    }
    const auto &function = owner.functions.at(found->second);
    return function.exported && function.live_ ? &function : nullptr;
}

// The FrameDescriptor a fun enters: a function of this module or an exported function of the batch, or null for
// an external fun nothing in the batch exports and for a fun only unreachable code creates (62C). lower_frames
// defines the descriptors of this module.
llvm::Constant *fun_frame(llvm::Module &output, const semantic::Module &module, const semantic::FunEntry &fun,
                          llvm::IntegerType *word) {
    const auto *target = fun.external ? exported(module, fun) : nullptr;
    if (fun.external ? !target : !output.getFunction(fun.symbol)) {
        return llvm::ConstantPointerNull::get(llvm::PointerType::get(output.getContext(), 0));
    }
    return frame_of(output, target ? target->symbol : fun.symbol, word);
}

// One abi::v1::FunDescriptor per semantic::FunEntry, in order; lowering addresses entries of `<prefix>.funs`.
llvm::GlobalVariable *funs(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                           llvm::GlobalVariable *descriptor, const std::string &prefix) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    auto *type = llvm::StructType::get(ptr, word, word, word, word, word, ptr);
    const auto value = [&](std::size_t number) { return llvm::ConstantInt::get(word, number); };
    std::vector<llvm::Constant *> entries;
    entries.reserve(module.funs.size());
    for (const auto &fun : module.funs) {
        entries.push_back(llvm::ConstantStruct::get(
            type, descriptor, atom_slot(output, utf8(fun.module)), atom_slot(output, utf8(fun.function)),
            value(fun.arity), value(fun.index), value(fun.external ? 1 : 0), fun_frame(output, module, fun, word)));
    }
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(type, entries.size()), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    prefix + ".funs");
}
} // namespace

// The module's fun cache (ModuleDescriptor::fun_cache): one zeroed word per fun; null without funs.
llvm::Constant *emit_fun_cache(llvm::Module &output, const std::string &prefix, llvm::IntegerType *word,
                               const std::size_t count) {
    if (count == 0) {
        return llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(output.getContext()));
    }
    auto *type = llvm::ArrayType::get(word, count);
    return new llvm::GlobalVariable(output, type, false, llvm::GlobalValue::PrivateLinkage,
                                    llvm::ConstantAggregateZero::get(type), prefix + ".fun_cache");
}

void emit_registration(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word) {
    llvm::IRBuilder<> builder(output.getContext());
    auto *ptr = builder.getPtrTy();
    auto *type = llvm::StructType::get(builder.getInt32Ty(), builder.getInt32Ty(), ptr, word, ptr, word, ptr, word, ptr,
                                       word, ptr, word, ptr, ptr, word, word, ptr, ptr, word, ptr, word);
    const auto name = utf8(module.name);
    std::size_t count = 0;
    auto *table = exports(output, module, word, count);
    std::size_t atom_count = 0;
    auto *atoms = emit_atom_table(output, module, word, atom_count);
    const auto prefix = semantic::encode_symbol({name, "", 0});
    // Records point back at the module descriptor, so it is created before its initializer.
    auto *descriptor = new llvm::GlobalVariable(output, type, true, llvm::GlobalValue::ExternalLinkage, nullptr,
                                                prefix + ".descriptor");
    auto *record_table = records(output, module, word, descriptor, prefix);
    const auto record_count = semantic::native_layouts(module).size();
    auto *fun_table = funs(output, module, word, descriptor, prefix);
    std::size_t feature_count = 0;
    auto *feature_table = features(output, module, word, feature_count);
    descriptor->setInitializer(llvm::ConstantStruct::get(
        type, builder.getInt32(abi::v1::version), builder.getInt32(word->getBitWidth()), spelling(output, name),
        llvm::ConstantInt::get(word, name.size()), table, llvm::ConstantInt::get(word, count), atoms,
        llvm::ConstantInt::get(word, atom_count), record_table, llvm::ConstantInt::get(word, record_count), fun_table,
        llvm::ConstantInt::get(word, module.funs.size()), emit_atom_cache(output, prefix, word, atom_count),
        llvm::ConstantPointerNull::get(ptr), llvm::ConstantInt::get(word, 0),
        llvm::ConstantInt::get(
            word, code_convention(llvm::Triple(output.getTargetTriple())) == llvm::CallingConv::PreserveNone ? 2 : 1),
        emit_fun_cache(output, prefix, word, module.funs.size()), feature_table,
        llvm::ConstantInt::get(word, feature_count), llvm::ConstantPointerNull::get(ptr),
        llvm::ConstantInt::get(word, 0)));
    auto *entry = llvm::Function::Create(llvm::FunctionType::get(builder.getInt8Ty(), {ptr}, false),
                                         llvm::GlobalValue::ExternalLinkage, prefix + ".register", output);
    builder.SetInsertPoint(llvm::BasicBlock::Create(output.getContext(), "entry", entry));
    auto service = output.getOrInsertFunction(services::symbol<services::RegisterModule>(output.getTargetTriple()),
                                              llvm::FunctionType::get(builder.getInt8Ty(), {ptr, ptr}, false));
    builder.CreateRet(builder.CreateCall(service, {entry->getArg(0), descriptor}));
    llvm::appendToUsed(output, {entry, descriptor});
}
} // namespace clause::codegen
