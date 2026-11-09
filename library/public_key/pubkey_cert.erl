%% OTP's pubkey_cert as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules):
%% certificate times in Gregorian seconds. Path validation is public_key's; the rest is declined.
-module(pubkey_cert).
-export([time_str_2_gregorian_sec/1]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    apply_fun/5,
    cert_auth_key_id/1,
    distribution_points/1,
    extensions_list/1,
    gen_test_certs/1,
    init_validation_state/3,
    is_fixed_dh_cert/1,
    is_issuer/2,
    is_self_signed/1,
    issuer_id/2,
    match_name/3,
    normalize_general_name/1,
    parse_and_check_validity_dates/1,
    prepare_for_next_cert/2,
    root_cert/2,
    select_extension/2,
    subject_id/1,
    validate_extensions/4,
    validate_issuer/4,
    validate_names/6,
    validate_signature/6,
    validate_time/3,
    verify_data/1,
    verify_fun/4,
    x509_pkix_sign_types/1
]).

% A utcTime (two-digit year in a sliding window) or generalTime in Gregorian seconds.
time_str_2_gregorian_sec(Time) -> clause_public_key_path:gregorian_seconds(Time).

apply_fun(_, _, _, _, _) -> erlang:error({not_implemented, apply_fun, 5}).
cert_auth_key_id(_) -> erlang:error({not_implemented, cert_auth_key_id, 1}).
distribution_points(_) -> erlang:error({not_implemented, distribution_points, 1}).
extensions_list(_) -> erlang:error({not_implemented, extensions_list, 1}).
gen_test_certs(_) -> erlang:error({not_implemented, gen_test_certs, 1}).
init_validation_state(_, _, _) -> erlang:error({not_implemented, init_validation_state, 3}).
is_fixed_dh_cert(_) -> erlang:error({not_implemented, is_fixed_dh_cert, 1}).
is_issuer(_, _) -> erlang:error({not_implemented, is_issuer, 2}).
is_self_signed(_) -> erlang:error({not_implemented, is_self_signed, 1}).
issuer_id(_, _) -> erlang:error({not_implemented, issuer_id, 2}).
match_name(_, _, _) -> erlang:error({not_implemented, match_name, 3}).
normalize_general_name(_) -> erlang:error({not_implemented, normalize_general_name, 1}).
parse_and_check_validity_dates(_) -> erlang:error({not_implemented, parse_and_check_validity_dates, 1}).
prepare_for_next_cert(_, _) -> erlang:error({not_implemented, prepare_for_next_cert, 2}).
root_cert(_, _) -> erlang:error({not_implemented, root_cert, 2}).
select_extension(_, _) -> erlang:error({not_implemented, select_extension, 2}).
subject_id(_) -> erlang:error({not_implemented, subject_id, 1}).
validate_extensions(_, _, _, _) -> erlang:error({not_implemented, validate_extensions, 4}).
validate_issuer(_, _, _, _) -> erlang:error({not_implemented, validate_issuer, 4}).
validate_names(_, _, _, _, _, _) -> erlang:error({not_implemented, validate_names, 6}).
validate_signature(_, _, _, _, _, _) -> erlang:error({not_implemented, validate_signature, 6}).
validate_time(_, _, _) -> erlang:error({not_implemented, validate_time, 3}).
verify_data(_) -> erlang:error({not_implemented, verify_data, 1}).
verify_fun(_, _, _, _) -> erlang:error({not_implemented, verify_fun, 4}).
x509_pkix_sign_types(_) -> erlang:error({not_implemented, x509_pkix_sign_types, 1}).
