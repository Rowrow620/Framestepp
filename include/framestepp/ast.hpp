#pragma once

#include "framestepp/source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace framestepp {

struct Expression;
struct Block;
struct Statement;

struct Name final {
    std::string text;
    Span span;
};

struct TypeName final {
    Name name;
};

struct Parameter final {
    Name name;
    TypeName type_name;
    Span span;
};

enum class UnaryOperator {
    negate,
    not_,
};

enum class BinaryOperator {
    add,
    subtract,
    multiply,
    divide,
    remainder,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

struct IntegerExpression final {
    std::int64_t value{0};
};

struct StringExpression final {
    std::string value;
};

struct BooleanExpression final {
    bool value{false};
};

struct IdentifierExpression final {
    std::string name;
};

struct GroupExpression final {
    std::unique_ptr<Expression> expression;
};

struct BlockExpression final {
    std::unique_ptr<Block> block;
};

struct UnaryExpression final {
    UnaryOperator operator_kind{UnaryOperator::negate};
    Span operator_span;
    std::unique_ptr<Expression> operand;
};

struct BinaryExpression final {
    std::unique_ptr<Expression> left;
    BinaryOperator operator_kind{BinaryOperator::add};
    Span operator_span;
    std::unique_ptr<Expression> right;
};

struct AssignmentExpression final {
    Name name;
    Span operator_span;
    std::unique_ptr<Expression> value;
};

struct CallExpression final {
    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;
};

struct IfExpression final {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Expression> else_branch;
};

using ExpressionKind =
    std::variant<IntegerExpression, StringExpression, BooleanExpression, IdentifierExpression,
                 GroupExpression, BlockExpression, UnaryExpression, BinaryExpression,
                 AssignmentExpression, CallExpression, IfExpression>;

struct Expression final {
    ExpressionKind kind;
    Span span;

    Expression(ExpressionKind expression_kind, Span expression_span);
    ~Expression();

    Expression(Expression&&) noexcept;
    Expression& operator=(Expression&&) noexcept;
    Expression(const Expression&) = delete;
    Expression& operator=(const Expression&) = delete;
};

struct Function final {
    Name name;
    std::vector<Parameter> parameters;
    std::optional<TypeName> return_type;
    std::unique_ptr<Block> body;

    Function(Name function_name, std::vector<Parameter> function_parameters,
             std::optional<TypeName> function_return_type, std::unique_ptr<Block> function_body);
    ~Function();

    Function(Function&&) noexcept;
    Function& operator=(Function&&) noexcept;
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
};

struct LetStatement final {
    bool mutable_binding{false};
    Name name;
    std::optional<TypeName> type_annotation;
    Expression initializer;
};

struct ReturnStatement final {
    std::optional<Expression> value;
};

struct ExpressionStatement final {
    Expression expression;
};

using StatementKind = std::variant<Function, LetStatement, ReturnStatement, ExpressionStatement>;

struct Statement final {
    StatementKind kind;
    Span span;

    Statement(StatementKind statement_kind, Span statement_span);
    ~Statement();

    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

struct Block final {
    std::vector<Statement> statements;
    std::unique_ptr<Expression> tail;
    Span span;

    Block(std::vector<Statement> block_statements, std::unique_ptr<Expression> tail_expression,
          Span block_span);
    ~Block();

    Block(Block&&) noexcept;
    Block& operator=(Block&&) noexcept;
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
};

struct Program final {
    std::vector<Statement> statements;
    Span span;

    Program(std::vector<Statement> program_statements, Span program_span);
    ~Program();

    Program(Program&&) noexcept;
    Program& operator=(Program&&) noexcept;
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
};

/// Produces a stable, indented preorder representation of a well-formed AST.
/// Recursive owning pointers must be non-null, as they are in parser output.
[[nodiscard]] std::string format_ast(const Program& program);

} // namespace framestepp
