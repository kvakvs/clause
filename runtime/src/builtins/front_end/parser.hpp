#pragma once
#include "../string/string.hpp"
#include "../support.hpp"
#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

// erl_parse's grammar over token terms (plan 21 step LIB31, docs/builtins.md#erl-parse): a recursive-descent parser
// that builds OTP's abstract format with the tokens' annotations, stops at the token OTP's LALR parser stops at and
// runs the grammar's actions (their errors included) when OTP's default reductions would.
namespace clause::runtime::builtins::front_end::parsing {
// The parse stopped before the token at `index`; past the tokens, it stopped at their end.
struct SyntaxError {
    std::size_t index;
};

// return_error/2 of an action: {error, {Location, erl_parse, Message}}.
struct ActionError {
    Term location;
    Term message;
};

// An action's message built by io_lib:format(Format, Arguments), which erl_parse.erl calls.
struct FormatError {
    Term location;
    std::string_view format;
    std::vector<Term> arguments;
};

// Whether the grammar reads an expression (OTP's expr) or a pattern (pat_expr).
enum class Mode : std::uint8_t { expression, pattern };

// The nonterminal an operand reduced to, which decides what '#' may follow it: expr_max, record_expr, map_expr or
// another expression.
enum class Shape : std::uint8_t { max, record, map, other };

// An operand of the operator grammar and its nonterminal.
struct Operand {
    Term term;
    Shape shape;
};

// prefix_op: + - bnot not.
bool is_prefix_operator(std::string_view category);

// The operators of expressions: their precedence and how they build their node.
struct Operator {
    enum class Build : std::uint8_t { match, op, call, remote };
    std::string_view name;
    int precedence;
    // Right operands of a left-associative operator bind one tighter; nonassociative ones may not chain.
    enum class Associativity : std::uint8_t { left, right, none } associativity;
    Build build;
    bool in_patterns;
};

// A list of tokens, one form: parse_form/1 of OTP (after its spec/callback/record rewrite).
class Parser final {
  public:
    Parser(ProcessContext &context, const Term &tokens);

    // form -> attribute dot | function dot, then the end of the tokens.
    Term form();

  private:
    // The tokens: the cursor, peeking, consuming and their parts.
    std::string_view category(std::size_t ahead = 0) const;
    bool at(std::string_view category, std::size_t ahead = 0) const;
    bool is_category(std::size_t index, std::string_view category) const;
    std::size_t take();
    std::size_t expect(std::string_view category);
    [[noreturn]] void fail() const;
    Term token(std::size_t index) const;
    // ?anno(Token) and element(3, Token).
    Term anno(std::size_t index) const;
    Term value(std::size_t index) const;

    // Terms of the abstract format.
    Term atom(std::string_view name) const;
    Term tuple(std::initializer_list<Term> items) const;
    Term list(const std::vector<Term> &items) const;
    Term first_anno(const Term &node) const;
    Term last_anno(const Term &node) const;
    Term location(const Term &anno) const;
    // A token's category as an atom: an operator's or reserved word's name.
    Term category_atom(std::size_t index) const;
    // return_error/2 at a location, or at a node's first location (ret_abstr_err/2).
    [[noreturn]] void error_at(const Term &location, std::string_view message) const;
    [[noreturn]] void error_at_node(const Term &node, std::string_view message) const;
    // error_bad_decl/2: "bad ~tw declaration".
    [[noreturn]] void bad_declaration(const Term &node, std::string_view what) const;

    // Functions and clauses.
    Term function();
    Term function_clause();
    // check_clauses/3 of build_function/1 and build_fun/2: the clauses without their names.
    std::vector<Term> checked_clauses(const std::vector<Term> &clauses) const;
    [[noreturn]] void head_mismatch(const Term &clause, std::size_t arity, const Term &name) const;
    // pat_argument_list: the arguments and the anno of '('.
    std::pair<std::vector<Term>, Term> pattern_arguments();
    std::vector<Term> arguments();
    Term clause_guard();
    Term guard();
    std::vector<Term> clause_body();
    std::vector<Term> exprs();

    // Attributes.
    Term attribute();
    Term plain_attribute(std::size_t name);
    Term attribute_values(std::size_t name, const Term &first, bool parenthesized);
    Term record_attribute();
    Term record_after_name(std::size_t record, bool parenthesized);
    Term native_record_attribute(std::size_t record, bool parenthesized);
    Term spec_attribute(std::size_t kind, bool parenthesized);

    // After the comma of an attribute value: typed record fields, or more expressions.
    struct Values {
        std::optional<Term> typed;
        std::vector<Term> exprs;
    };

    Values values_after_comma();
    // '{' ... '}' whose elements may be typed (typed_record_fields), else a tuple expression to continue.
    std::optional<Term> typed_tuple(std::vector<Term> &untyped);
    Term typed_element(bool &typed);
    void close_attribute(bool parenthesized);

    // The actions of attributes.
    Term build_attribute(std::size_t name, const std::vector<Term> &values) const;
    Term made_attribute(std::size_t name, const Term &content) const;
    Term module_attribute(std::size_t name, const std::vector<Term> &values) const;
    Term export_attribute(std::size_t name, const std::vector<Term> &values) const;
    Term import_attribute(std::size_t name, const std::vector<Term> &values) const;
    Term native_record_definition(std::size_t name, const std::vector<Term> &values) const;
    Term file_attribute(std::size_t name, const std::vector<Term> &values) const;
    Term build_doc(std::size_t name, const std::vector<Term> &values) const;
    std::optional<Term> plain_doc(const Term &node) const;
    std::optional<Term> doc_file(const Term &node) const;
    Term doc_map(const Term &map, bool doc) const;
    struct AttributeBuilder;
    static const std::array<AttributeBuilder, 8> ATTRIBUTE_BUILDERS;
    Term build_typed_attribute(std::size_t name, const Term &definition) const;
    Term build_type_definition(std::size_t name, const Term &call, const Term &type) const;
    Term build_record(std::size_t record, std::string_view tag, const Term &name, const Term &fields) const;
    Term build_type_spec(std::size_t kind, const Term &function, const std::vector<Term> &specs) const;
    Term spec_arity(const Term &spec) const;
    std::vector<Term> farity_list(const Term &list) const;
    std::vector<Term> var_list(const Term &list) const;
    std::vector<Term> native_record_names(const Term &list) const;
    Term record_tuple(const Term &tuple) const;
    std::vector<Term> record_fields(const std::vector<Term> &fields) const;
    Term record_field(const Term &field) const;
    Term attribute_farity(const Term &node) const;
    Term farity_cons(const Term &node) const;
    Term farity_map(const Term &node) const;
    // term/1: normalise/1, else "bad attribute" at the node.
    Term attribute_term(const Term &node) const;

    // Expressions: the operator grammar.
    Term expression(Mode mode = Mode::expression, int minimum = 0);
    Term continue_expression(Operand left, Mode mode, int minimum);
    Operand unary(Mode mode);
    const Operator *infix(Mode mode) const;
    Term binary(const Term &left, const Operator &operation, Mode mode);
    Operand postfix(Operand left);
    Operand hash_suffix(const Term &left, Shape shape);
    Operand field_or_update(const Term &left, std::size_t hash, const Term &id);
    // record_name -> atom | var | reserved_word, as an atom node.
    Term record_name();
    // The name's atom: element(3) of an atom or var, the category of a reserved word.
    Term record_name_value(const Term &name) const;
    bool at_record_name() const;
    bool at_expression_start() const;

    // Expressions: primaries.
    Operand primary(Mode mode);
    std::optional<Operand> atomic();
    std::optional<Operand> bracketed(Mode mode);
    Term expr_max();
    Operand hash_primary(Mode mode);
    Term list_expression(Mode mode);
    Term list_tail(std::vector<Term> &elements, std::size_t open);
    Term tuple_expression();
    Term binary_expression(Mode mode);
    Term bin_element(std::optional<Term> value);
    Term bit_types();
    Term strings();
    Term sigil();
    Term map_tuple();
    Term map_field();
    Operand map_creation(std::size_t hash, Mode mode, bool comprehension_only);
    Term record_tuple_fields();
    Term record_field_expression();
    Term paren(Mode mode);
    Term block();
    struct Keyword;
    static const std::array<Keyword, 7> KEYWORDS;

    // Expressions: comprehensions.
    std::vector<Term> qualifiers();
    Term qualifier();
    Term zip(const Term &first);
    Term generator(const Term &left, bool map_field);

    // Expressions: control.
    Term if_expression();
    Term case_expression();
    std::vector<Term> cr_clauses();
    Term cr_clause();
    Term receive_expression();
    Term try_expression();
    std::vector<Term> try_clauses();
    Term try_clause();
    Term class_clause();
    Term maybe_expression();
    std::vector<Term> maybe_body();
    Term fun_expression();
    Term fun_reference(std::size_t fun);
    Term fun_clauses(std::size_t fun);
    Term fun_clause();

    // Types.
    Term top_type();
    std::vector<Term> top_types();
    Term type(int minimum = 0);
    Term type_operand();
    Term type_primary();
    Term bracketed_type();
    Term atom_type();
    Term list_type();
    Term map_type();
    Term map_pair_type();
    Term record_type(std::size_t hash);
    std::vector<Term> field_types();
    Term tuple_type();
    Term binary_type();
    // The unit after '<<' and an optional base: the second variable on.
    Term unit_type(std::size_t open, std::optional<Term> base, std::size_t variable);
    // build_bin_type/2.
    Term bin_type(std::initializer_list<std::size_t> variables, const Term &type) const;
    Term fun_type_after_fun();
    Term fun_type();
    std::vector<Term> type_sigs();
    Term type_sig();
    Term type_guard();
    // build_constraint/2.
    Term constraint(const Term &variable, const Term &type) const;
    Term lift_unions(const Term &left, const Term &right) const;
    // abstract2(0, Anno): {integer, Line, 0}.
    Term zero_at(const Term &anno) const;

    ProcessContext &context_;
    string::Terms terms_;
    // The tokens and, for each, its category: element(1) when an atom; an improper list's tail ends them.
    std::vector<Term> tokens_;
    std::vector<std::string_view> categories_;
    bool improper_ = false;
    std::size_t pos_ = 0;
    // Where the last binary expression began and ended: a b_generate needs a bare binary pattern.
    std::pair<std::size_t, std::size_t> binary_span_{SIZE_MAX, SIZE_MAX};
};
} // namespace clause::runtime::builtins::front_end::parsing
