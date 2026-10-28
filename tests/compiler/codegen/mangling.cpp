#include "codegen/runtime_symbols.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace clause::codegen;

// Hand-written spellings previously hardcoded in codegen, each verified against Clang declarations.
struct Expected {
    std::string_view msvc64;
    std::string_view msvc32;
    std::string_view itanium64;
    std::string_view itanium32;
};

// Triple strings parse without a configured backend, so every ABI is checked on every host.
struct Platform {
    const char *triple;
    std::string_view Expected::*spelling;
};

constexpr std::array platforms{
    Platform{"x86_64-pc-windows-msvc", &Expected::msvc64},
    Platform{"i686-pc-windows-msvc", &Expected::msvc32},
    Platform{"x86_64-unknown-linux-gnu", &Expected::itanium64},
    Platform{"aarch64-unknown-linux-gnu", &Expected::itanium64},
    Platform{"arm64-apple-macosx14.0.0", &Expected::itanium64},
    Platform{"i686-unknown-linux-gnu", &Expected::itanium32},
    Platform{"armv7-unknown-linux-gnueabihf", &Expected::itanium32},
};

// Compare one service's mangled spelling with the former literal on every supported platform.
template <typename Service> void check(const Expected &expected) {
    for (const auto &platform : platforms) {
        const auto actual = services::symbol<Service>(llvm::Triple(platform.triple));
        if (actual != expected.*platform.spelling) {
            throw std::runtime_error(std::string(platform.triple) + ": got " + std::string(actual) + ", expected " +
                                     std::string(expected.*platform.spelling));
        }
    }
}

// Every runtime service keeps the exact spelling previously hardcoded in codegen.
int main() {
    try {
        check<services::Integer>({"?CLAUSE_integer_v1@@YAEPEAXPEBD_KPEA_K@Z", "?CLAUSE_integer_v1@@YAEPAXPBDIPAI@Z",
                                  "_Z17CLAUSE_integer_v1PvPKcmPm", "_Z17CLAUSE_integer_v1PvPKcjPj"});
        check<services::Float>({"?CLAUSE_float_v1@@YAEPEAXPEBD_KPEA_K@Z", "?CLAUSE_float_v1@@YAEPAXPBDIPAI@Z",
                                "_Z15CLAUSE_float_v1PvPKcmPm", "_Z15CLAUSE_float_v1PvPKcjPj"});
        check<services::Immediate>({"?CLAUSE_immediate_v1@@YAEPEAXE_K1PEA_K@Z", "?CLAUSE_immediate_v1@@YAEPAXEIIPAI@Z",
                                    "_Z19CLAUSE_immediate_v1PvhmmPm", "_Z19CLAUSE_immediate_v1PvhjjPj"});
        check<services::Construct>({"?CLAUSE_construct_v1@@YAEPEAXEPEB_K_KPEA_K@Z",
                                    "?CLAUSE_construct_v1@@YAEPAXEPBIIPAI@Z", "_Z19CLAUSE_construct_v1PvhPKmmPm",
                                    "_Z19CLAUSE_construct_v1PvhPKjjPj"});
        check<services::Inspect>({"?CLAUSE_inspect_v1@@YAEPEAXE_K1PEA_K@Z", "?CLAUSE_inspect_v1@@YAEPAXEIIPAI@Z",
                                  "_Z17CLAUSE_inspect_v1PvhmmPm", "_Z17CLAUSE_inspect_v1PvhjjPj"});
        check<services::Bits>({"?CLAUSE_bits_v1@@YAEPEAXEPEB_K_KPEA_K@Z", "?CLAUSE_bits_v1@@YAEPAXEPBIIPAI@Z",
                               "_Z14CLAUSE_bits_v1PvhPKmmPm", "_Z14CLAUSE_bits_v1PvhPKjjPj"});
        check<services::Map>({"?CLAUSE_map_v1@@YAEPEAXEPEB_K_KPEA_K@Z", "?CLAUSE_map_v1@@YAEPAXEPBIIPAI@Z",
                              "_Z13CLAUSE_map_v1PvhPKmmPm", "_Z13CLAUSE_map_v1PvhPKjjPj"});
        check<services::Record>({"?CLAUSE_record_v1@@YAEPEAXEEPEBXPEB_K_KPEA_K@Z",
                                 "?CLAUSE_record_v1@@YAEPAXEEPBXPBIIPAI@Z", "_Z16CLAUSE_record_v1PvhhPKvPKmmPm",
                                 "_Z16CLAUSE_record_v1PvhhPKvPKjjPj"});
        check<services::MakeFun>({"?CLAUSE_make_fun_v1@@YAEPEAXPEBXPEB_K_KPEA_K@Z",
                                  "?CLAUSE_make_fun_v1@@YAEPAXPBXPBIIPAI@Z", "_Z18CLAUSE_make_fun_v1PvPKvPKmmPm",
                                  "_Z18CLAUSE_make_fun_v1PvPKvPKjjPj"});
        check<services::Apply>({"?CLAUSE_apply_v1@@YAPEBXPEAX_K1PEA_K@Z", "?CLAUSE_apply_v1@@YAPBXPAXIIPAI@Z",
                                "_Z15CLAUSE_apply_v1PvmmPm", "_Z15CLAUSE_apply_v1PvjjPj"});
        check<services::Call>({"?CLAUSE_call_v1@@YAPEBXPEAX_K11PEA_K@Z", "?CLAUSE_call_v1@@YAPBXPAXIIIPAI@Z",
                               "_Z14CLAUSE_call_v1PvmmmPm", "_Z14CLAUSE_call_v1PvjjjPj"});
        check<services::ApplyList>({"?CLAUSE_apply_list_v1@@YAPEBXPEAX_K1PEA_K@Z",
                                    "?CLAUSE_apply_list_v1@@YAPBXPAXIIPAI@Z", "_Z20CLAUSE_apply_list_v1PvmmPm",
                                    "_Z20CLAUSE_apply_list_v1PvjjPj"});
        check<services::CallList>({"?CLAUSE_call_list_v1@@YAPEBXPEAX_K11PEA_K@Z",
                                   "?CLAUSE_call_list_v1@@YAPBXPAXIIIPAI@Z", "_Z19CLAUSE_call_list_v1PvmmmPm",
                                   "_Z19CLAUSE_call_list_v1PvjjjPj"});
        check<services::MakeExternalFun>(
            {"?CLAUSE_make_external_fun_v1@@YAEPEAX_K11PEA_K@Z", "?CLAUSE_make_external_fun_v1@@YAEPAXIIIPAI@Z",
             "_Z27CLAUSE_make_external_fun_v1PvmmmPm", "_Z27CLAUSE_make_external_fun_v1PvjjjPj"});
        check<services::Display>({"?CLAUSE_display_v1@@YAEPEAX_KPEA_K@Z", "?CLAUSE_display_v1@@YAEPAXIPAI@Z",
                                  "_Z17CLAUSE_display_v1PvmPm", "_Z17CLAUSE_display_v1PvjPj"});
        check<services::BuiltinFrame>({"?CLAUSE_builtin_frame_v1@@YAPEBXPEAX_K@Z",
                                       "?CLAUSE_builtin_frame_v1@@YAPBXPAXI@Z", "_Z23CLAUSE_builtin_frame_v1Pvm",
                                       "_Z23CLAUSE_builtin_frame_v1Pvj"});
        check<services::BuiltinCall>({"?CLAUSE_builtin_call_v1@@YA_KPEAX_KPEB_K@Z",
                                      "?CLAUSE_builtin_call_v1@@YAIPAXIPBI@Z", "_Z22CLAUSE_builtin_call_v1PvmPKm",
                                      "_Z22CLAUSE_builtin_call_v1PvjPKj"});
        check<services::Receive>({"?CLAUSE_receive_v1@@YAEPEAXEPEA_K@Z", "?CLAUSE_receive_v1@@YAEPAXEPAI@Z",
                                  "_Z17CLAUSE_receive_v1PvhPm", "_Z17CLAUSE_receive_v1PvhPj"});
        check<services::WaitFrame>({"?CLAUSE_wait_frame_v1@@YAPEBXPEAX@Z", "?CLAUSE_wait_frame_v1@@YAPBXPAX@Z",
                                    "_Z20CLAUSE_wait_frame_v1Pv", "_Z20CLAUSE_wait_frame_v1Pv"});
        check<services::Halt>({"?CLAUSE_halt_v1@@YAEPEAX_K@Z", "?CLAUSE_halt_v1@@YAEPAXI@Z", "_Z14CLAUSE_halt_v1Pvm",
                               "_Z14CLAUSE_halt_v1Pvj"});
        check<services::Main>({"?CLAUSE_main_v1@@YAHHPEAPEADPEBX@Z", "?CLAUSE_main_v1@@YAHHPAPADPBX@Z",
                               "_Z14CLAUSE_main_v1iPPcPKv", "_Z14CLAUSE_main_v1iPPcPKv"});
        check<services::CryptoExtension>({"?CLAUSE_crypto_extension_v1@@YAPEBXXZ",
                                          "?CLAUSE_crypto_extension_v1@@YAPBXXZ", "_Z26CLAUSE_crypto_extension_v1v",
                                          "_Z26CLAUSE_crypto_extension_v1v"});
        check<services::CryptoFipsExtension>(
            {"?CLAUSE_crypto_fips_extension_v1@@YAPEBXXZ", "?CLAUSE_crypto_fips_extension_v1@@YAPBXXZ",
             "_Z31CLAUSE_crypto_fips_extension_v1v", "_Z31CLAUSE_crypto_fips_extension_v1v"});
        check<services::SslExtension>({"?CLAUSE_ssl_extension_v1@@YAPEBXXZ", "?CLAUSE_ssl_extension_v1@@YAPBXXZ",
                                       "_Z23CLAUSE_ssl_extension_v1v", "_Z23CLAUSE_ssl_extension_v1v"});
        check<services::Exact>({"?CLAUSE_exact_v1@@YAEPEAX_K1@Z", "?CLAUSE_exact_v1@@YAEPAXII@Z",
                                "_Z15CLAUSE_exact_v1Pvmm", "_Z15CLAUSE_exact_v1Pvjj"});
        check<services::CallFailed>({"?CLAUSE_call_failed_v2@@YAEPEAX@Z", "?CLAUSE_call_failed_v2@@YAEPAX@Z",
                                     "_Z21CLAUSE_call_failed_v2Pv", "_Z21CLAUSE_call_failed_v2Pv"});
        check<services::Raise>({"?CLAUSE_raise_v2@@YAEPEAXW4ErrorReason@v1@abi@clause@@_K@Z",
                                "?CLAUSE_raise_v2@@YAEPAXW4ErrorReason@v1@abi@clause@@I@Z",
                                "_Z15CLAUSE_raise_v2PvN6clause3abi2v111ErrorReasonEm",
                                "_Z15CLAUSE_raise_v2PvN6clause3abi2v111ErrorReasonEj"});
        check<services::Catch>({"?CLAUSE_catch_v1@@YAEPEAXPEA_K@Z", "?CLAUSE_catch_v1@@YAEPAXPAI@Z",
                                "_Z15CLAUSE_catch_v1PvPm", "_Z15CLAUSE_catch_v1PvPj"});
        check<services::Exception>({"?CLAUSE_exception_v2@@YAEPEAXPEA_K11@Z", "?CLAUSE_exception_v2@@YAEPAXPAI11@Z",
                                    "_Z19CLAUSE_exception_v2PvPmS0_S0_", "_Z19CLAUSE_exception_v2PvPjS0_S0_"});
        check<services::Reraise>({"?CLAUSE_reraise_v2@@YAEPEAX_K11@Z", "?CLAUSE_reraise_v2@@YAEPAXIII@Z",
                                  "_Z17CLAUSE_reraise_v2Pvmmm", "_Z17CLAUSE_reraise_v2Pvjjj"});
        check<services::Error>({"?CLAUSE_error_v1@@YAEPEAX_K1@Z", "?CLAUSE_error_v1@@YAEPAXII@Z",
                                "_Z15CLAUSE_error_v1Pvmm", "_Z15CLAUSE_error_v1Pvjj"});
        check<services::Enter>({"?CLAUSE_enter_v1@@YAPEAXPEAXPEBX@Z", "?CLAUSE_enter_v1@@YAPAXPAXPBX@Z",
                                "_Z15CLAUSE_enter_v1PvPKv", "_Z15CLAUSE_enter_v1PvPKv"});
        check<services::Tail>({"?CLAUSE_tail_v1@@YAPEAXPEAXPEBX@Z", "?CLAUSE_tail_v1@@YAPAXPAXPBX@Z",
                               "_Z14CLAUSE_tail_v1PvPKv", "_Z14CLAUSE_tail_v1PvPKv"});
        check<services::Return>({"?CLAUSE_return_v1@@YAPEAXPEAX_K@Z", "?CLAUSE_return_v1@@YAPAXPAXI@Z",
                                 "_Z16CLAUSE_return_v1Pvm", "_Z16CLAUSE_return_v1Pvj"});
        check<services::Safepoint>({"?CLAUSE_safepoint_v1@@YAXPEAX@Z", "?CLAUSE_safepoint_v1@@YAXPAX@Z",
                                    "_Z19CLAUSE_safepoint_v1Pv", "_Z19CLAUSE_safepoint_v1Pv"});
        check<services::Frame>({"?CLAUSE_frame_v1@@YAPEA_KPEAX@Z", "?CLAUSE_frame_v1@@YAPAIPAX@Z",
                                "_Z15CLAUSE_frame_v1Pv", "_Z15CLAUSE_frame_v1Pv"});
        check<services::Registers>({"?CLAUSE_registers_v1@@YAPEA_KPEAX@Z", "?CLAUSE_registers_v1@@YAPAIPAX@Z",
                                    "_Z19CLAUSE_registers_v1Pv", "_Z19CLAUSE_registers_v1Pv"});
        check<services::Invoke>({"?CLAUSE_invoke_v1@@YA_KPEAXPEBXPEB_K@Z", "?CLAUSE_invoke_v1@@YAIPAXPBXPBI@Z",
                                 "_Z16CLAUSE_invoke_v1PvPKvPKm", "_Z16CLAUSE_invoke_v1PvPKvPKj"});
        check<services::RegisterModule>({"?CLAUSE_register_module_v4@@YAEPEAXPEBX@Z",
                                         "?CLAUSE_register_module_v4@@YAEPAXPBX@Z",
                                         "_Z25CLAUSE_register_module_v4PvPKv", "_Z25CLAUSE_register_module_v4PvPKv"});
        check<services::Atom>({"?CLAUSE_atom_v3@@YA_KPEAX_KPEBX@Z", "?CLAUSE_atom_v3@@YAIPAXIPBX@Z",
                               "_Z14CLAUSE_atom_v3PvmPKv", "_Z14CLAUSE_atom_v3PvjPKv"});
        check<services::Literal>({"?CLAUSE_literal_v1@@YA_KPEAX_KPEBX@Z", "?CLAUSE_literal_v1@@YAIPAXIPBX@Z",
                                  "_Z17CLAUSE_literal_v1PvmPKv", "_Z17CLAUSE_literal_v1PvjPKv"});
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
