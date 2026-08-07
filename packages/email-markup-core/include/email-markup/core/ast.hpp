#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "email-markup/core/source.hpp"

namespace email_markup {

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Parameter {
    std::string name;
    std::string expression;
    SourceRange range;
};

struct TextNode { std::string text; };
struct ExpressionNode { std::string expression; };
struct ComponentNode {
    std::string name;
    std::vector<Parameter> parameters;
    std::vector<NodePtr> children;
    bool self_closing{};
};
struct IfNode {
    std::string condition;
    std::vector<NodePtr> then_nodes;
    std::vector<NodePtr> else_nodes;
};
struct ForNode {
    std::string variable;
    std::string expression;
    std::vector<NodePtr> body;
};
struct SlotNode {
    std::string name{"default"};
    std::vector<NodePtr> body;
    bool reference{};
};
struct IncludeNode { std::string expression; };

using NodeValue = std::variant<TextNode, ExpressionNode, ComponentNode, IfNode,
                               ForNode, SlotNode, IncludeNode>;

struct Node {
    SourceRange range;
    NodeValue value;
};

struct PropDeclaration {
    std::string name;
    std::string type;
    bool optional{};
    std::string default_expression;
    bool has_default{};
    double minimum{};
    double maximum{};
    bool has_range{};
    std::string comparison;
    double bound{};
    SourceRange range;
};

struct SlotDeclaration {
    std::string name;
    bool required{};
    SourceRange range;
};

struct ComponentDefinition {
    std::string name;
    std::vector<PropDeclaration> props;
    std::vector<SlotDeclaration> slots;
    std::vector<NodePtr> body;
    SourceRange range;
};

struct StyleDefinition {
    std::string name;
    std::string declarations;
    SourceRange range;
};

struct TokenDefinition {
    std::string name;
    std::string expression;
    SourceRange range;
};

struct MediaDefinition {
    std::string query;
    std::string css;
    SourceRange range;
};

struct Document {
    SourceId source{};
    std::vector<NodePtr> nodes;
    std::unordered_map<std::string, ComponentDefinition> components;
    std::unordered_map<std::string, StyleDefinition> styles;
    std::unordered_map<std::string, TokenDefinition> tokens;
    std::vector<MediaDefinition> media;
};

}  // namespace email_markup
