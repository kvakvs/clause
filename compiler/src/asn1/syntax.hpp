#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the parsed form of an ASN.1 module. Values, objects, object
// sets and actual parameters stay token ranges until the checker knows what governs them (X.680/681/683 notation
// is ambiguous without that).
#include "lexer.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clause::asn1 {
// A half-open range of a module's tokens, parsed later.
struct TokenRange {
    std::uint32_t begin_ = 0;
    std::uint32_t end_ = 0;

    bool empty() const { return begin_ == end_; }
};

// A name as written, `Name` or `Module.Name`; `token_` is where it starts.
struct Reference {
    std::string module_;
    std::string name_;
    std::uint32_t token_ = 0;
};

enum class TagClass : std::uint8_t { universal, application, context, private_ };

enum class TagMode : std::uint8_t { module_default, implicit, explicit_ };

struct TagSyntax {
    TagClass class_ = TagClass::context;
    // The tag number: a number or a defined value.
    TokenRange number_;
    TagMode mode_ = TagMode::module_default;
};

// `name(value)` in INTEGER, ENUMERATED and BIT STRING lists; the value is empty for an enumeration item without one.
struct NamedNumber {
    std::string name_;
    TokenRange value_;
    std::uint32_t token_ = 0;
};

struct TypeSyntax;
struct ConstraintSyntax;

enum class Presence : std::uint8_t { required, optional, default_ };

// One entry of a SEQUENCE, SET or CHOICE body in written order.
struct ComponentSyntax {
    enum class Kind : std::uint8_t { named, components_of, ellipsis, group_begin, group_end };
    Kind kind_ = Kind::named;
    std::string name_;
    std::shared_ptr<TypeSyntax> type_;
    Presence presence_ = Presence::required;
    TokenRange default_;
    // `[[n:` version number of a group; 0 when not given.
    std::uint32_t version_ = 0;
    std::uint32_t token_ = 0;

    // An entry that is not a component: `...`, `[[` or `]]`.
    static ComponentSyntax marker(const Kind kind, const std::uint32_t token) {
        ComponentSyntax result;
        result.kind_ = kind;
        result.token_ = token;
        return result;
    }
};

enum class TypeKind : std::uint8_t {
    reference,
    boolean,
    integer,
    bit_string,
    octet_string,
    null,
    object_identifier,
    relative_oid,
    real,
    enumerated,
    sequence,
    set,
    sequence_of,
    set_of,
    choice,
    character_string_type,
    restricted_string,
    utc_time,
    generalized_time,
    object_descriptor,
    external,
    embedded_pdv,
    any,
    // `CLASS.&field` (ObjectClassFieldType) and `object.&field` / `ObjectSet.&field` (InformationFromObjects).
    class_field,
    object_field,
    instance_of,
    // `identifier < Type`.
    selection,
    type_identifier,
    abstract_syntax,
};

struct TypeSyntax {
    TypeKind kind_ = TypeKind::reference;
    std::vector<TagSyntax> tags_;
    // Serial constraints `T (C1) (C2)` in written order.
    std::vector<std::shared_ptr<ConstraintSyntax>> constraints_;
    // A reference; the class of a class field or INSTANCE OF; the object of an object field; the CHOICE of a
    // selection type is `element_`.
    Reference reference_;
    // The actual parameters of a parameterized reference, each parsed once its formal parameter is known.
    std::vector<TokenRange> arguments_;
    bool parameterized_ = false;
    // `&a.&b` of a class or object field.
    std::vector<std::string> fields_;
    // The restricted character string type name (IA5String, ...).
    std::string string_;
    // Named numbers of INTEGER, named bits of BIT STRING, ENUMERATED items (`...` as an item named "...").
    std::vector<NamedNumber> named_;
    std::vector<ComponentSyntax> components_;
    // SEQUENCE OF and SET OF element, selection type's CHOICE; the element's name (`SEQUENCE OF x T`).
    std::shared_ptr<TypeSyntax> element_;
    std::string element_name_;
    // `SEQUENCE SIZE (..) OF` and `SEQUENCE (..) OF`: the constraint on the collection itself.
    std::shared_ptr<ConstraintSyntax> collection_constraint_;
    // ANY DEFINED BY component; selection type alternative.
    std::string identifier_;
    std::uint32_t token_ = 0;
};

// One element or operator of a constraint specification.
struct ConstraintSyntax {
    enum class Kind : std::uint8_t {
        union_,
        intersection,
        except,
        all_except,
        // A root set, `...`, and an optional addition set: `children_` holds [root, additions]; either may be null.
        extensible,
        single_value,
        value_range,
        size,
        from,
        contained_subtype,
        inner_type,
        inner_types,
        containing,
        user_defined,
        table,
        pattern,
        // `!value` exception specification: ignored by encoders.
        exception,
    };
    Kind kind_ = Kind::single_value;
    std::vector<std::shared_ptr<ConstraintSyntax>> children_;
    // The value, the lower endpoint, the PATTERN value or the object set.
    TokenRange value_;
    // The upper endpoint of a range; `MIN`/`MAX` stay tokens. `lower_open_`/`upper_open_` mark `<`.
    TokenRange upper_;
    bool lower_open_ = false;
    bool upper_open_ = false;
    // CONTAINING / INCLUDES / contained subtype: the type; ENCODED BY value in `value_`.
    std::shared_ptr<TypeSyntax> type_;

    // WITH COMPONENTS: named constraints; `partial_` for `{..., ...}`.
    struct Named {
        std::string name_;
        std::shared_ptr<ConstraintSyntax> constraint_;
        // PRESENT, ABSENT, OPTIONAL or empty.
        std::string presence_;
    };

    std::vector<Named> named_;
    bool partial_ = false;

    // Component relation constraint: `{@a.b, @.c}` at-notations, each a level count of leading dots and names.
    struct AtNotation {
        std::uint32_t level_ = 0;
        std::vector<std::string> path_;
    };

    std::vector<AtNotation> at_;
    std::uint32_t token_ = 0;
};

// One field of an information object class.
struct FieldSpecSyntax {
    std::string name_;
    bool type_field_ = false;
    // The governor of a value, value set, object or object set field; empty for a type field.
    std::shared_ptr<TypeSyntax> type_;
    bool unique_ = false;
    Presence presence_ = Presence::required;
    TokenRange default_;
    std::uint32_t token_ = 0;
};

// One item of a WITH SYNTAX list: a literal word or comma, a field, or an optional group.
struct SyntaxItem {
    enum class Kind : std::uint8_t { literal, field, optional };
    // A word or comma to match, a field to set, or an optional group `[...]`.
    Kind kind_ = Kind::literal;
    // The literal's spelling or the field's name.
    std::string text_;
    // The items of an optional group.
    std::vector<SyntaxItem> items_;
};

struct ClassSyntax {
    std::vector<FieldSpecSyntax> fields_;
    bool with_syntax_ = false;
    std::vector<SyntaxItem> syntax_;
};

// A formal parameter `Governor : Name` or `Name`.
struct ParameterSyntax {
    std::shared_ptr<TypeSyntax> governor_;
    std::string name_;
    std::uint32_t token_ = 0;
};

struct AssignmentSyntax {
    enum class Kind : std::uint8_t {
        // `Name ::= Type` (a type, or a class when the type names one).
        type,
        // `Name ::= CLASS {...}`.
        class_,
        // `name Governor ::= Value` (a value, or an object when the governor is a class).
        value,
        // `Name Governor ::= {...}` (a value set, or an object set when the governor is a class).
        set,
    };
    Kind kind_ = Kind::type;
    std::string name_;
    std::vector<ParameterSyntax> parameters_;
    bool parameterized_ = false;
    std::shared_ptr<TypeSyntax> type_;
    std::shared_ptr<ClassSyntax> class_;
    // The governor of a value or set assignment and its right-hand side.
    std::shared_ptr<TypeSyntax> governor_;
    TokenRange value_;
    std::uint32_t token_ = 0;
};

struct ImportSyntax {
    std::vector<std::string> symbols_;
    std::string module_;
    std::uint32_t token_ = 0;
};

struct ModuleSyntax {
    std::string name_;
    std::string file_;
    std::vector<Token> tokens_;
    TagMode tag_default_ = TagMode::explicit_;
    bool automatic_tags_ = false;
    bool extensibility_implied_ = false;
    std::vector<ImportSyntax> imports_;
    // Exported names; `exports_all_` when EXPORTS is absent or `EXPORTS ALL`.
    std::vector<std::string> exports_;
    bool exports_all_ = true;
    std::vector<AssignmentSyntax> assignments_;

    Location location(std::uint32_t token) const;
};

// Parse one module from its tokens (several modules per file are allowed by X.680 but rare: the first is used,
// with the rest returned by parse_file).
std::vector<std::shared_ptr<ModuleSyntax>> parse_file(std::string_view text, const std::string &file);
} // namespace clause::asn1
