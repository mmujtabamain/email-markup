#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "email-markup/core/source.hpp"

namespace email_markup
{

    struct Node;
    using NodePtr = std::shared_ptr<Node>;

    struct Parameter
    {
        std::string name;
        std::string expression;
        SourceRange range;
    };

    struct TextNode
    {
        std::string text;
    };
    struct ExpressionNode
    {
        std::string expression;
    };
    struct ComponentNode
    {
        std::string name;
        std::vector<Parameter> parameters;
        std::vector<NodePtr> children;
        bool self_closing{};
    };
    struct IfNode
    {
        std::string condition;
        std::vector<NodePtr> then_nodes;
        std::vector<NodePtr> else_nodes;
    };
    struct ForNode
    {
        std::string variable;
        std::string expression;
        std::vector<NodePtr> body;
    };
    struct SlotNode
    {
        std::string name{"default"};
        std::vector<NodePtr> body;
        bool reference{};
    };
    struct IncludeNode
    {
        std::string expression;
    };
    struct EngineNode
    {
        std::string expression;
    };
    struct DeferredCallNode
    {
        std::string name;
        std::string payload;
        std::vector<NodePtr> children;
        bool self_closing{};
        bool bare{};
    };

    using NodeValue = std::variant<TextNode, ExpressionNode, ComponentNode, IfNode,
                                   ForNode, SlotNode, IncludeNode, EngineNode,
                                   DeferredCallNode>;

    struct Node
    {
        SourceRange range;
        NodeValue value;
    };

    enum class DeclarationType
    {
        string,
        integer,
        decimal,
        number,
        boolean,
        name,
        url,
        email,
        color,
        raw,
        path,
        condition
    };

    enum class ComparisonOperator
    {
        greater,
        greater_equal,
        less,
        less_equal
    };

    struct NumericBound
    {
        std::string spelling;
        bool integer{};
        SourceRange range;
    };

    struct RangeConstraint
    {
        NumericBound minimum;
        NumericBound maximum;
    };

    struct ComparisonConstraint
    {
        ComparisonOperator operation{};
        NumericBound bound;
        SourceRange operator_range;
    };

    struct Declaration
    {
        std::string name;
        std::string type;
        DeclarationType value_type{DeclarationType::string};
        bool has_explicit_type{};
        bool optional{};
        std::string default_expression;
        bool has_default{};
        std::optional<RangeConstraint> range_constraint;
        std::optional<ComparisonConstraint> comparison_constraint;
        SourceRange range;
        SourceRange name_range;
        SourceRange optional_range;
        SourceRange type_range;
        SourceRange default_range;
    };

    using PropDeclaration = Declaration;

    struct SlotDeclaration
    {
        std::string name;
        bool required{};
        SourceRange range;
    };

    struct ComponentDefinition
    {
        std::string name;
        std::vector<PropDeclaration> props;
        std::vector<SlotDeclaration> slots;
        std::vector<NodePtr> body;
        SourceRange range;
    };

    struct StyleDefinition
    {
        std::string name;
        std::string declarations;
        SourceRange range;
    };

    struct TokenDefinition
    {
        std::string name;
        std::string expression;
        SourceRange range;
    };

    struct MediaDefinition
    {
        std::string query;
        std::string css;
        SourceRange range;
    };

    struct Document
    {
        SourceId source{};
        std::vector<NodePtr> nodes;
        std::unordered_map<std::string, ComponentDefinition> components;
        std::unordered_map<std::string, StyleDefinition> styles;
        std::unordered_map<std::string, TokenDefinition> tokens;
        std::vector<MediaDefinition> media;
    };

} // namespace email_markup
