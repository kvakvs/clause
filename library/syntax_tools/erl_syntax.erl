%% Clause's erl_syntax (plan 21 LIB77E2, docs/builtins.md#erl-syntax): OTP's syntax trees, every function a
%% runtime builtin with a clause_builtin_placeholder body, but the *_literal and name functions, Erlang over io_lib.
-module(erl_syntax).
-export([
    abstract/1,
    add_ann/2,
    add_postcomments/2,
    add_precomments/2,
    annotated_type/2,
    annotated_type_body/1,
    annotated_type_name/1,
    application/2,
    application/3,
    application_arguments/1,
    application_operator/1,
    arity_qualifier/2,
    arity_qualifier_argument/1,
    arity_qualifier_body/1,
    atom/1,
    atom_literal/1,
    atom_literal/2,
    atom_name/1,
    atom_value/1,
    attribute/1,
    attribute/2,
    attribute_arguments/1,
    attribute_name/1,
    binary/1,
    binary_comp/2,
    binary_comp_body/1,
    binary_comp_template/1,
    binary_field/1,
    binary_field/2,
    binary_field/3,
    binary_field_body/1,
    binary_field_size/1,
    binary_field_types/1,
    binary_fields/1,
    binary_generator/2,
    binary_generator_body/1,
    binary_generator_pattern/1,
    bitstring_type/2,
    bitstring_type_m/1,
    bitstring_type_n/1,
    block_expr/1,
    block_expr_body/1,
    case_expr/2,
    case_expr_argument/1,
    case_expr_clauses/1,
    catch_expr/1,
    catch_expr_body/1,
    char/1,
    char_literal/1,
    char_literal/2,
    char_value/1,
    class_qualifier/2,
    class_qualifier/3,
    class_qualifier_argument/1,
    class_qualifier_body/1,
    class_qualifier_stacktrace/1,
    clause/2,
    clause/3,
    clause_body/1,
    clause_guard/1,
    clause_patterns/1,
    comment/1,
    comment/2,
    comment_padding/1,
    comment_text/1,
    compact_list/1,
    concrete/1,
    conjunction/1,
    conjunction_body/1,
    cons/2,
    constrained_function_type/2,
    constrained_function_type_argument/1,
    constrained_function_type_body/1,
    constraint/2,
    constraint_argument/1,
    constraint_body/1,
    copy_ann/2,
    copy_attrs/2,
    copy_comments/2,
    copy_pos/2,
    data/1,
    disjunction/1,
    disjunction_body/1,
    else_expr/1,
    else_expr_clauses/1,
    eof_marker/0,
    error_marker/1,
    error_marker_info/1,
    flatten_form_list/1,
    float/1,
    float_literal/1,
    float_value/1,
    form_list/1,
    form_list_elements/1,
    fun_expr/1,
    fun_expr_arity/1,
    fun_expr_clauses/1,
    fun_type/0,
    function/2,
    function_arity/1,
    function_clauses/1,
    function_name/1,
    function_type/1,
    function_type/2,
    function_type_arguments/1,
    function_type_return/1,
    generator/2,
    generator_body/1,
    generator_pattern/1,
    get_ann/1,
    get_attrs/1,
    get_pos/1,
    get_postcomments/1,
    get_precomments/1,
    has_comments/1,
    if_expr/1,
    if_expr_clauses/1,
    implicit_fun/1,
    implicit_fun/2,
    implicit_fun/3,
    implicit_fun_name/1,
    infix_expr/3,
    infix_expr_left/1,
    infix_expr_operator/1,
    infix_expr_right/1,
    integer/1,
    integer_literal/1,
    integer_range_type/2,
    integer_range_type_high/1,
    integer_range_type_low/1,
    integer_value/1,
    is_atom/2,
    is_char/2,
    is_form/1,
    is_integer/2,
    is_leaf/1,
    is_list_skeleton/1,
    is_literal/1,
    is_proper_list/1,
    is_string/2,
    is_tree/1,
    join_comments/2,
    list/1,
    list/2,
    list_comp/2,
    list_comp_body/1,
    list_comp_template/1,
    list_elements/1,
    list_head/1,
    list_length/1,
    list_prefix/1,
    list_suffix/1,
    list_tail/1,
    macro/1,
    macro/2,
    macro_arguments/1,
    macro_name/1,
    make_tree/2,
    map_comp/2,
    map_comp_body/1,
    map_comp_template/1,
    map_expr/1,
    map_expr/2,
    map_expr_argument/1,
    map_expr_fields/1,
    map_field_assoc/2,
    map_field_assoc_name/1,
    map_field_assoc_value/1,
    map_field_exact/2,
    map_field_exact_name/1,
    map_field_exact_value/1,
    map_generator/2,
    map_generator_body/1,
    map_generator_pattern/1,
    map_type/0,
    map_type/1,
    map_type_assoc/2,
    map_type_assoc_name/1,
    map_type_assoc_value/1,
    map_type_exact/2,
    map_type_exact_name/1,
    map_type_exact_value/1,
    map_type_fields/1,
    match_expr/2,
    match_expr_body/1,
    match_expr_pattern/1,
    maybe_expr/1,
    maybe_expr/2,
    maybe_expr_body/1,
    maybe_expr_else/1,
    maybe_match_expr/2,
    maybe_match_expr_body/1,
    maybe_match_expr_pattern/1,
    meta/1,
    module_qualifier/2,
    module_qualifier_argument/1,
    module_qualifier_body/1,
    named_fun_expr/2,
    named_fun_expr_arity/1,
    named_fun_expr_clauses/1,
    named_fun_expr_name/1,
    nil/0,
    normalize_list/1,
    operator/1,
    operator_literal/1,
    operator_name/1,
    parentheses/1,
    parentheses_body/1,
    prefix_expr/2,
    prefix_expr_argument/1,
    prefix_expr_operator/1,
    receive_expr/1,
    receive_expr/3,
    receive_expr_action/1,
    receive_expr_clauses/1,
    receive_expr_timeout/1,
    record_access/3,
    record_access_argument/1,
    record_access_field/1,
    record_access_type/1,
    record_expr/2,
    record_expr/3,
    record_expr_argument/1,
    record_expr_fields/1,
    record_expr_type/1,
    record_field/1,
    record_field/2,
    record_field_name/1,
    record_field_value/1,
    record_index_expr/2,
    record_index_expr_field/1,
    record_index_expr_type/1,
    record_type/2,
    record_type_field/2,
    record_type_field_name/1,
    record_type_field_type/1,
    record_type_fields/1,
    record_type_name/1,
    remove_comments/1,
    revert/1,
    revert_forms/1,
    set_ann/2,
    set_attrs/2,
    set_pos/2,
    set_postcomments/2,
    set_precomments/2,
    size_qualifier/2,
    size_qualifier_argument/1,
    size_qualifier_body/1,
    strict_binary_generator/2,
    strict_binary_generator_body/1,
    strict_binary_generator_pattern/1,
    strict_generator/2,
    strict_generator_body/1,
    strict_generator_pattern/1,
    strict_map_generator/2,
    strict_map_generator_body/1,
    strict_map_generator_pattern/1,
    string/1,
    string_literal/1,
    string_literal/2,
    string_value/1,
    subtrees/1,
    text/1,
    text_string/1,
    tree/1,
    tree/2,
    try_after_expr/2,
    try_expr/2,
    try_expr/3,
    try_expr/4,
    try_expr_after/1,
    try_expr_body/1,
    try_expr_clauses/1,
    try_expr_handlers/1,
    tuple/1,
    tuple_elements/1,
    tuple_size/1,
    tuple_type/0,
    tuple_type/1,
    tuple_type_elements/1,
    type/1,
    type_application/2,
    type_application/3,
    type_application_arguments/1,
    type_application_name/1,
    type_union/1,
    type_union_types/1,
    typed_record_field/2,
    typed_record_field_body/1,
    typed_record_field_type/1,
    underscore/0,
    update_tree/2,
    user_type_application/2,
    user_type_application_arguments/1,
    user_type_application_name/1,
    variable/1,
    variable_literal/1,
    variable_name/1,
    warning_marker/1,
    warning_marker_info/1,
    zip_generator/1,
    zip_generator_body/1
]).
-include("erl_syntax.types.hrl").

% Native abstract/1.
abstract(_) -> clause_builtin_placeholder.

% Native add_ann/2.
add_ann(_, _) -> clause_builtin_placeholder.

% Native add_postcomments/2.
add_postcomments(_, _) -> clause_builtin_placeholder.

% Native add_precomments/2.
add_precomments(_, _) -> clause_builtin_placeholder.

% Native annotated_type/2.
annotated_type(_, _) -> clause_builtin_placeholder.

% Native annotated_type_body/1.
annotated_type_body(_) -> clause_builtin_placeholder.

% Native annotated_type_name/1.
annotated_type_name(_) -> clause_builtin_placeholder.

% Native application/2.
application(_, _) -> clause_builtin_placeholder.

% Native application/3.
application(_, _, _) -> clause_builtin_placeholder.

% Native application_arguments/1.
application_arguments(_) -> clause_builtin_placeholder.

% Native application_operator/1.
application_operator(_) -> clause_builtin_placeholder.

% Native arity_qualifier/2.
arity_qualifier(_, _) -> clause_builtin_placeholder.

% Native arity_qualifier_argument/1.
arity_qualifier_argument(_) -> clause_builtin_placeholder.

% Native arity_qualifier_body/1.
arity_qualifier_body(_) -> clause_builtin_placeholder.

% Native atom/1.
atom(_) -> clause_builtin_placeholder.

% The atom as source text, quoted when needed.
atom_literal(Node) -> atom_literal(Node, latin1).

% The atom as source text, quoted when needed.
atom_literal(Node, latin1) ->
    io_lib:write_atom_as_latin1(erl_syntax:atom_value(Node));
atom_literal(Node, Encoding) when Encoding =:= utf8; Encoding =:= unicode ->
    io_lib:write_atom(erl_syntax:atom_value(Node)).

% The name of an atom node.
atom_name(Node) -> atom_to_list(erl_syntax:atom_value(Node)).

% Native atom_value/1.
atom_value(_) -> clause_builtin_placeholder.

% Native attribute/1.
attribute(_) -> clause_builtin_placeholder.

% Native attribute/2.
attribute(_, _) -> clause_builtin_placeholder.

% Native attribute_arguments/1.
attribute_arguments(_) -> clause_builtin_placeholder.

% Native attribute_name/1.
attribute_name(_) -> clause_builtin_placeholder.

% Native binary/1.
binary(_) -> clause_builtin_placeholder.

% Native binary_comp/2.
binary_comp(_, _) -> clause_builtin_placeholder.

% Native binary_comp_body/1.
binary_comp_body(_) -> clause_builtin_placeholder.

% Native binary_comp_template/1.
binary_comp_template(_) -> clause_builtin_placeholder.

% Native binary_field/1.
binary_field(_) -> clause_builtin_placeholder.

% Native binary_field/2.
binary_field(_, _) -> clause_builtin_placeholder.

% Native binary_field/3.
binary_field(_, _, _) -> clause_builtin_placeholder.

% Native binary_field_body/1.
binary_field_body(_) -> clause_builtin_placeholder.

% Native binary_field_size/1.
binary_field_size(_) -> clause_builtin_placeholder.

% Native binary_field_types/1.
binary_field_types(_) -> clause_builtin_placeholder.

% Native binary_fields/1.
binary_fields(_) -> clause_builtin_placeholder.

% Native binary_generator/2.
binary_generator(_, _) -> clause_builtin_placeholder.

% Native binary_generator_body/1.
binary_generator_body(_) -> clause_builtin_placeholder.

% Native binary_generator_pattern/1.
binary_generator_pattern(_) -> clause_builtin_placeholder.

% Native bitstring_type/2.
bitstring_type(_, _) -> clause_builtin_placeholder.

% Native bitstring_type_m/1.
bitstring_type_m(_) -> clause_builtin_placeholder.

% Native bitstring_type_n/1.
bitstring_type_n(_) -> clause_builtin_placeholder.

% Native block_expr/1.
block_expr(_) -> clause_builtin_placeholder.

% Native block_expr_body/1.
block_expr_body(_) -> clause_builtin_placeholder.

% Native case_expr/2.
case_expr(_, _) -> clause_builtin_placeholder.

% Native case_expr_argument/1.
case_expr_argument(_) -> clause_builtin_placeholder.

% Native case_expr_clauses/1.
case_expr_clauses(_) -> clause_builtin_placeholder.

% Native catch_expr/1.
catch_expr(_) -> clause_builtin_placeholder.

% Native catch_expr_body/1.
catch_expr_body(_) -> clause_builtin_placeholder.

% Native char/1.
char(_) -> clause_builtin_placeholder.

% The character as source text ($c).
char_literal(Node) -> char_literal(Node, latin1).

% The character as source text ($c).
char_literal(Node, latin1) ->
    io_lib:write_char_as_latin1(erl_syntax:char_value(Node));
char_literal(Node, Encoding) when Encoding =:= utf8; Encoding =:= unicode ->
    io_lib:write_char(erl_syntax:char_value(Node)).

% Native char_value/1.
char_value(_) -> clause_builtin_placeholder.

% Native class_qualifier/2.
class_qualifier(_, _) -> clause_builtin_placeholder.

% Native class_qualifier/3.
class_qualifier(_, _, _) -> clause_builtin_placeholder.

% Native class_qualifier_argument/1.
class_qualifier_argument(_) -> clause_builtin_placeholder.

% Native class_qualifier_body/1.
class_qualifier_body(_) -> clause_builtin_placeholder.

% Native class_qualifier_stacktrace/1.
class_qualifier_stacktrace(_) -> clause_builtin_placeholder.

% Native clause/2.
clause(_, _) -> clause_builtin_placeholder.

% Native clause/3.
clause(_, _, _) -> clause_builtin_placeholder.

% Native clause_body/1.
clause_body(_) -> clause_builtin_placeholder.

% Native clause_guard/1.
clause_guard(_) -> clause_builtin_placeholder.

% Native clause_patterns/1.
clause_patterns(_) -> clause_builtin_placeholder.

% Native comment/1.
comment(_) -> clause_builtin_placeholder.

% Native comment/2.
comment(_, _) -> clause_builtin_placeholder.

% Native comment_padding/1.
comment_padding(_) -> clause_builtin_placeholder.

% Native comment_text/1.
comment_text(_) -> clause_builtin_placeholder.

% Native compact_list/1.
compact_list(_) -> clause_builtin_placeholder.

% Native concrete/1.
concrete(_) -> clause_builtin_placeholder.

% Native conjunction/1.
conjunction(_) -> clause_builtin_placeholder.

% Native conjunction_body/1.
conjunction_body(_) -> clause_builtin_placeholder.

% Native cons/2.
cons(_, _) -> clause_builtin_placeholder.

% Native constrained_function_type/2.
constrained_function_type(_, _) -> clause_builtin_placeholder.

% Native constrained_function_type_argument/1.
constrained_function_type_argument(_) -> clause_builtin_placeholder.

% Native constrained_function_type_body/1.
constrained_function_type_body(_) -> clause_builtin_placeholder.

% Native constraint/2.
constraint(_, _) -> clause_builtin_placeholder.

% Native constraint_argument/1.
constraint_argument(_) -> clause_builtin_placeholder.

% Native constraint_body/1.
constraint_body(_) -> clause_builtin_placeholder.

% Native copy_ann/2.
copy_ann(_, _) -> clause_builtin_placeholder.

% Native copy_attrs/2.
copy_attrs(_, _) -> clause_builtin_placeholder.

% Native copy_comments/2.
copy_comments(_, _) -> clause_builtin_placeholder.

% Native copy_pos/2.
copy_pos(_, _) -> clause_builtin_placeholder.

% Native data/1.
data(_) -> clause_builtin_placeholder.

% Native disjunction/1.
disjunction(_) -> clause_builtin_placeholder.

% Native disjunction_body/1.
disjunction_body(_) -> clause_builtin_placeholder.

% Native else_expr/1.
else_expr(_) -> clause_builtin_placeholder.

% Native else_expr_clauses/1.
else_expr_clauses(_) -> clause_builtin_placeholder.

% Native eof_marker/0.
eof_marker() -> clause_builtin_placeholder.

% Native error_marker/1.
error_marker(_) -> clause_builtin_placeholder.

% Native error_marker_info/1.
error_marker_info(_) -> clause_builtin_placeholder.

% Native flatten_form_list/1.
flatten_form_list(_) -> clause_builtin_placeholder.

% Native float/1.
float(_) -> clause_builtin_placeholder.

% The float as text.
float_literal(Node) -> float_to_list(erl_syntax:float_value(Node)).

% Native float_value/1.
float_value(_) -> clause_builtin_placeholder.

% Native form_list/1.
form_list(_) -> clause_builtin_placeholder.

% Native form_list_elements/1.
form_list_elements(_) -> clause_builtin_placeholder.

% Native fun_expr/1.
fun_expr(_) -> clause_builtin_placeholder.

% Native fun_expr_arity/1.
fun_expr_arity(_) -> clause_builtin_placeholder.

% Native fun_expr_clauses/1.
fun_expr_clauses(_) -> clause_builtin_placeholder.

% Native fun_type/0.
fun_type() -> clause_builtin_placeholder.

% Native function/2.
function(_, _) -> clause_builtin_placeholder.

% Native function_arity/1.
function_arity(_) -> clause_builtin_placeholder.

% Native function_clauses/1.
function_clauses(_) -> clause_builtin_placeholder.

% Native function_name/1.
function_name(_) -> clause_builtin_placeholder.

% Native function_type/1.
function_type(_) -> clause_builtin_placeholder.

% Native function_type/2.
function_type(_, _) -> clause_builtin_placeholder.

% Native function_type_arguments/1.
function_type_arguments(_) -> clause_builtin_placeholder.

% Native function_type_return/1.
function_type_return(_) -> clause_builtin_placeholder.

% Native generator/2.
generator(_, _) -> clause_builtin_placeholder.

% Native generator_body/1.
generator_body(_) -> clause_builtin_placeholder.

% Native generator_pattern/1.
generator_pattern(_) -> clause_builtin_placeholder.

% Native get_ann/1.
get_ann(_) -> clause_builtin_placeholder.

% Native get_attrs/1.
get_attrs(_) -> clause_builtin_placeholder.

% Native get_pos/1.
get_pos(_) -> clause_builtin_placeholder.

% Native get_postcomments/1.
get_postcomments(_) -> clause_builtin_placeholder.

% Native get_precomments/1.
get_precomments(_) -> clause_builtin_placeholder.

% Native has_comments/1.
has_comments(_) -> clause_builtin_placeholder.

% Native if_expr/1.
if_expr(_) -> clause_builtin_placeholder.

% Native if_expr_clauses/1.
if_expr_clauses(_) -> clause_builtin_placeholder.

% Native implicit_fun/1.
implicit_fun(_) -> clause_builtin_placeholder.

% Native implicit_fun/2.
implicit_fun(_, _) -> clause_builtin_placeholder.

% Native implicit_fun/3.
implicit_fun(_, _, _) -> clause_builtin_placeholder.

% Native implicit_fun_name/1.
implicit_fun_name(_) -> clause_builtin_placeholder.

% Native infix_expr/3.
infix_expr(_, _, _) -> clause_builtin_placeholder.

% Native infix_expr_left/1.
infix_expr_left(_) -> clause_builtin_placeholder.

% Native infix_expr_operator/1.
infix_expr_operator(_) -> clause_builtin_placeholder.

% Native infix_expr_right/1.
infix_expr_right(_) -> clause_builtin_placeholder.

% Native integer/1.
integer(_) -> clause_builtin_placeholder.

% The integer as text.
integer_literal(Node) -> integer_to_list(erl_syntax:integer_value(Node)).

% Native integer_range_type/2.
integer_range_type(_, _) -> clause_builtin_placeholder.

% Native integer_range_type_high/1.
integer_range_type_high(_) -> clause_builtin_placeholder.

% Native integer_range_type_low/1.
integer_range_type_low(_) -> clause_builtin_placeholder.

% Native integer_value/1.
integer_value(_) -> clause_builtin_placeholder.

% Native is_atom/2.
is_atom(_, _) -> clause_builtin_placeholder.

% Native is_char/2.
is_char(_, _) -> clause_builtin_placeholder.

% Native is_form/1.
is_form(_) -> clause_builtin_placeholder.

% Native is_integer/2.
is_integer(_, _) -> clause_builtin_placeholder.

% Native is_leaf/1.
is_leaf(_) -> clause_builtin_placeholder.

% Native is_list_skeleton/1.
is_list_skeleton(_) -> clause_builtin_placeholder.

% Native is_literal/1.
is_literal(_) -> clause_builtin_placeholder.

% Native is_proper_list/1.
is_proper_list(_) -> clause_builtin_placeholder.

% Native is_string/2.
is_string(_, _) -> clause_builtin_placeholder.

% Native is_tree/1.
is_tree(_) -> clause_builtin_placeholder.

% Native join_comments/2.
join_comments(_, _) -> clause_builtin_placeholder.

% Native list/1.
list(_) -> clause_builtin_placeholder.

% Native list/2.
list(_, _) -> clause_builtin_placeholder.

% Native list_comp/2.
list_comp(_, _) -> clause_builtin_placeholder.

% Native list_comp_body/1.
list_comp_body(_) -> clause_builtin_placeholder.

% Native list_comp_template/1.
list_comp_template(_) -> clause_builtin_placeholder.

% Native list_elements/1.
list_elements(_) -> clause_builtin_placeholder.

% Native list_head/1.
list_head(_) -> clause_builtin_placeholder.

% Native list_length/1.
list_length(_) -> clause_builtin_placeholder.

% Native list_prefix/1.
list_prefix(_) -> clause_builtin_placeholder.

% Native list_suffix/1.
list_suffix(_) -> clause_builtin_placeholder.

% Native list_tail/1.
list_tail(_) -> clause_builtin_placeholder.

% Native macro/1.
macro(_) -> clause_builtin_placeholder.

% Native macro/2.
macro(_, _) -> clause_builtin_placeholder.

% Native macro_arguments/1.
macro_arguments(_) -> clause_builtin_placeholder.

% Native macro_name/1.
macro_name(_) -> clause_builtin_placeholder.

% Native make_tree/2.
make_tree(_, _) -> clause_builtin_placeholder.

% Native map_comp/2.
map_comp(_, _) -> clause_builtin_placeholder.

% Native map_comp_body/1.
map_comp_body(_) -> clause_builtin_placeholder.

% Native map_comp_template/1.
map_comp_template(_) -> clause_builtin_placeholder.

% Native map_expr/1.
map_expr(_) -> clause_builtin_placeholder.

% Native map_expr/2.
map_expr(_, _) -> clause_builtin_placeholder.

% Native map_expr_argument/1.
map_expr_argument(_) -> clause_builtin_placeholder.

% Native map_expr_fields/1.
map_expr_fields(_) -> clause_builtin_placeholder.

% Native map_field_assoc/2.
map_field_assoc(_, _) -> clause_builtin_placeholder.

% Native map_field_assoc_name/1.
map_field_assoc_name(_) -> clause_builtin_placeholder.

% Native map_field_assoc_value/1.
map_field_assoc_value(_) -> clause_builtin_placeholder.

% Native map_field_exact/2.
map_field_exact(_, _) -> clause_builtin_placeholder.

% Native map_field_exact_name/1.
map_field_exact_name(_) -> clause_builtin_placeholder.

% Native map_field_exact_value/1.
map_field_exact_value(_) -> clause_builtin_placeholder.

% Native map_generator/2.
map_generator(_, _) -> clause_builtin_placeholder.

% Native map_generator_body/1.
map_generator_body(_) -> clause_builtin_placeholder.

% Native map_generator_pattern/1.
map_generator_pattern(_) -> clause_builtin_placeholder.

% Native map_type/0.
map_type() -> clause_builtin_placeholder.

% Native map_type/1.
map_type(_) -> clause_builtin_placeholder.

% Native map_type_assoc/2.
map_type_assoc(_, _) -> clause_builtin_placeholder.

% Native map_type_assoc_name/1.
map_type_assoc_name(_) -> clause_builtin_placeholder.

% Native map_type_assoc_value/1.
map_type_assoc_value(_) -> clause_builtin_placeholder.

% Native map_type_exact/2.
map_type_exact(_, _) -> clause_builtin_placeholder.

% Native map_type_exact_name/1.
map_type_exact_name(_) -> clause_builtin_placeholder.

% Native map_type_exact_value/1.
map_type_exact_value(_) -> clause_builtin_placeholder.

% Native map_type_fields/1.
map_type_fields(_) -> clause_builtin_placeholder.

% Native match_expr/2.
match_expr(_, _) -> clause_builtin_placeholder.

% Native match_expr_body/1.
match_expr_body(_) -> clause_builtin_placeholder.

% Native match_expr_pattern/1.
match_expr_pattern(_) -> clause_builtin_placeholder.

% Native maybe_expr/1.
maybe_expr(_) -> clause_builtin_placeholder.

% Native maybe_expr/2.
maybe_expr(_, _) -> clause_builtin_placeholder.

% Native maybe_expr_body/1.
maybe_expr_body(_) -> clause_builtin_placeholder.

% Native maybe_expr_else/1.
maybe_expr_else(_) -> clause_builtin_placeholder.

% Native maybe_match_expr/2.
maybe_match_expr(_, _) -> clause_builtin_placeholder.

% Native maybe_match_expr_body/1.
maybe_match_expr_body(_) -> clause_builtin_placeholder.

% Native maybe_match_expr_pattern/1.
maybe_match_expr_pattern(_) -> clause_builtin_placeholder.

% Native meta/1.
meta(_) -> clause_builtin_placeholder.

% Native module_qualifier/2.
module_qualifier(_, _) -> clause_builtin_placeholder.

% Native module_qualifier_argument/1.
module_qualifier_argument(_) -> clause_builtin_placeholder.

% Native module_qualifier_body/1.
module_qualifier_body(_) -> clause_builtin_placeholder.

% Native named_fun_expr/2.
named_fun_expr(_, _) -> clause_builtin_placeholder.

% Native named_fun_expr_arity/1.
named_fun_expr_arity(_) -> clause_builtin_placeholder.

% Native named_fun_expr_clauses/1.
named_fun_expr_clauses(_) -> clause_builtin_placeholder.

% Native named_fun_expr_name/1.
named_fun_expr_name(_) -> clause_builtin_placeholder.

% Native nil/0.
nil() -> clause_builtin_placeholder.

% Native normalize_list/1.
normalize_list(_) -> clause_builtin_placeholder.

% Native operator/1.
operator(_) -> clause_builtin_placeholder.

% The name of an operator node.
operator_literal(Node) -> atom_to_list(erl_syntax:operator_name(Node)).

% Native operator_name/1.
operator_name(_) -> clause_builtin_placeholder.

% Native parentheses/1.
parentheses(_) -> clause_builtin_placeholder.

% Native parentheses_body/1.
parentheses_body(_) -> clause_builtin_placeholder.

% Native prefix_expr/2.
prefix_expr(_, _) -> clause_builtin_placeholder.

% Native prefix_expr_argument/1.
prefix_expr_argument(_) -> clause_builtin_placeholder.

% Native prefix_expr_operator/1.
prefix_expr_operator(_) -> clause_builtin_placeholder.

% Native receive_expr/1.
receive_expr(_) -> clause_builtin_placeholder.

% Native receive_expr/3.
receive_expr(_, _, _) -> clause_builtin_placeholder.

% Native receive_expr_action/1.
receive_expr_action(_) -> clause_builtin_placeholder.

% Native receive_expr_clauses/1.
receive_expr_clauses(_) -> clause_builtin_placeholder.

% Native receive_expr_timeout/1.
receive_expr_timeout(_) -> clause_builtin_placeholder.

% Native record_access/3.
record_access(_, _, _) -> clause_builtin_placeholder.

% Native record_access_argument/1.
record_access_argument(_) -> clause_builtin_placeholder.

% Native record_access_field/1.
record_access_field(_) -> clause_builtin_placeholder.

% Native record_access_type/1.
record_access_type(_) -> clause_builtin_placeholder.

% Native record_expr/2.
record_expr(_, _) -> clause_builtin_placeholder.

% Native record_expr/3.
record_expr(_, _, _) -> clause_builtin_placeholder.

% Native record_expr_argument/1.
record_expr_argument(_) -> clause_builtin_placeholder.

% Native record_expr_fields/1.
record_expr_fields(_) -> clause_builtin_placeholder.

% Native record_expr_type/1.
record_expr_type(_) -> clause_builtin_placeholder.

% Native record_field/1.
record_field(_) -> clause_builtin_placeholder.

% Native record_field/2.
record_field(_, _) -> clause_builtin_placeholder.

% Native record_field_name/1.
record_field_name(_) -> clause_builtin_placeholder.

% Native record_field_value/1.
record_field_value(_) -> clause_builtin_placeholder.

% Native record_index_expr/2.
record_index_expr(_, _) -> clause_builtin_placeholder.

% Native record_index_expr_field/1.
record_index_expr_field(_) -> clause_builtin_placeholder.

% Native record_index_expr_type/1.
record_index_expr_type(_) -> clause_builtin_placeholder.

% Native record_type/2.
record_type(_, _) -> clause_builtin_placeholder.

% Native record_type_field/2.
record_type_field(_, _) -> clause_builtin_placeholder.

% Native record_type_field_name/1.
record_type_field_name(_) -> clause_builtin_placeholder.

% Native record_type_field_type/1.
record_type_field_type(_) -> clause_builtin_placeholder.

% Native record_type_fields/1.
record_type_fields(_) -> clause_builtin_placeholder.

% Native record_type_name/1.
record_type_name(_) -> clause_builtin_placeholder.

% Native remove_comments/1.
remove_comments(_) -> clause_builtin_placeholder.

% Native revert/1.
revert(_) -> clause_builtin_placeholder.

% Native revert_forms/1.
revert_forms(_) -> clause_builtin_placeholder.

% Native set_ann/2.
set_ann(_, _) -> clause_builtin_placeholder.

% Native set_attrs/2.
set_attrs(_, _) -> clause_builtin_placeholder.

% Native set_pos/2.
set_pos(_, _) -> clause_builtin_placeholder.

% Native set_postcomments/2.
set_postcomments(_, _) -> clause_builtin_placeholder.

% Native set_precomments/2.
set_precomments(_, _) -> clause_builtin_placeholder.

% Native size_qualifier/2.
size_qualifier(_, _) -> clause_builtin_placeholder.

% Native size_qualifier_argument/1.
size_qualifier_argument(_) -> clause_builtin_placeholder.

% Native size_qualifier_body/1.
size_qualifier_body(_) -> clause_builtin_placeholder.

% Native strict_binary_generator/2.
strict_binary_generator(_, _) -> clause_builtin_placeholder.

% Native strict_binary_generator_body/1.
strict_binary_generator_body(_) -> clause_builtin_placeholder.

% Native strict_binary_generator_pattern/1.
strict_binary_generator_pattern(_) -> clause_builtin_placeholder.

% Native strict_generator/2.
strict_generator(_, _) -> clause_builtin_placeholder.

% Native strict_generator_body/1.
strict_generator_body(_) -> clause_builtin_placeholder.

% Native strict_generator_pattern/1.
strict_generator_pattern(_) -> clause_builtin_placeholder.

% Native strict_map_generator/2.
strict_map_generator(_, _) -> clause_builtin_placeholder.

% Native strict_map_generator_body/1.
strict_map_generator_body(_) -> clause_builtin_placeholder.

% Native strict_map_generator_pattern/1.
strict_map_generator_pattern(_) -> clause_builtin_placeholder.

% Native string/1.
string(_) -> clause_builtin_placeholder.

% The string as source text.
string_literal(Node) -> string_literal(Node, latin1).

% The string as source text.
string_literal(Node, latin1) ->
    io_lib:write_string_as_latin1(erl_syntax:string_value(Node));
string_literal(Node, Encoding) when Encoding =:= utf8; Encoding =:= unicode ->
    io_lib:write_string(erl_syntax:string_value(Node)).

% Native string_value/1.
string_value(_) -> clause_builtin_placeholder.

% Native subtrees/1.
subtrees(_) -> clause_builtin_placeholder.

% Native text/1.
text(_) -> clause_builtin_placeholder.

% Native text_string/1.
text_string(_) -> clause_builtin_placeholder.

% Native tree/1.
tree(_) -> clause_builtin_placeholder.

% Native tree/2.
tree(_, _) -> clause_builtin_placeholder.

% Native try_after_expr/2.
try_after_expr(_, _) -> clause_builtin_placeholder.

% Native try_expr/2.
try_expr(_, _) -> clause_builtin_placeholder.

% Native try_expr/3.
try_expr(_, _, _) -> clause_builtin_placeholder.

% Native try_expr/4.
try_expr(_, _, _, _) -> clause_builtin_placeholder.

% Native try_expr_after/1.
try_expr_after(_) -> clause_builtin_placeholder.

% Native try_expr_body/1.
try_expr_body(_) -> clause_builtin_placeholder.

% Native try_expr_clauses/1.
try_expr_clauses(_) -> clause_builtin_placeholder.

% Native try_expr_handlers/1.
try_expr_handlers(_) -> clause_builtin_placeholder.

% Native tuple/1.
tuple(_) -> clause_builtin_placeholder.

% Native tuple_elements/1.
tuple_elements(_) -> clause_builtin_placeholder.

% Native tuple_size/1.
tuple_size(_) -> clause_builtin_placeholder.

% Native tuple_type/0.
tuple_type() -> clause_builtin_placeholder.

% Native tuple_type/1.
tuple_type(_) -> clause_builtin_placeholder.

% Native tuple_type_elements/1.
tuple_type_elements(_) -> clause_builtin_placeholder.

% Native type/1.
type(_) -> clause_builtin_placeholder.

% Native type_application/2.
type_application(_, _) -> clause_builtin_placeholder.

% Native type_application/3.
type_application(_, _, _) -> clause_builtin_placeholder.

% Native type_application_arguments/1.
type_application_arguments(_) -> clause_builtin_placeholder.

% Native type_application_name/1.
type_application_name(_) -> clause_builtin_placeholder.

% Native type_union/1.
type_union(_) -> clause_builtin_placeholder.

% Native type_union_types/1.
type_union_types(_) -> clause_builtin_placeholder.

% Native typed_record_field/2.
typed_record_field(_, _) -> clause_builtin_placeholder.

% Native typed_record_field_body/1.
typed_record_field_body(_) -> clause_builtin_placeholder.

% Native typed_record_field_type/1.
typed_record_field_type(_) -> clause_builtin_placeholder.

% Native underscore/0.
underscore() -> clause_builtin_placeholder.

% Native update_tree/2.
update_tree(_, _) -> clause_builtin_placeholder.

% Native user_type_application/2.
user_type_application(_, _) -> clause_builtin_placeholder.

% Native user_type_application_arguments/1.
user_type_application_arguments(_) -> clause_builtin_placeholder.

% Native user_type_application_name/1.
user_type_application_name(_) -> clause_builtin_placeholder.

% Native variable/1.
variable(_) -> clause_builtin_placeholder.

% The name of a variable node.
variable_literal(Node) -> atom_to_list(erl_syntax:variable_name(Node)).

% Native variable_name/1.
variable_name(_) -> clause_builtin_placeholder.

% Native warning_marker/1.
warning_marker(_) -> clause_builtin_placeholder.

% Native warning_marker_info/1.
warning_marker_info(_) -> clause_builtin_placeholder.

% Native zip_generator/1.
zip_generator(_) -> clause_builtin_placeholder.

% Native zip_generator_body/1.
zip_generator_body(_) -> clause_builtin_placeholder.
