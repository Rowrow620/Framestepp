#include "framestepp/ast.hpp"

#include <cassert>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace framestepp {

Expression::Expression(ExpressionKind expression_kind, const Span expression_span)
    : kind{std::move(expression_kind)}, span{expression_span} {}

Expression::~Expression() = default;
Expression::Expression(Expression&&) noexcept = default;
Expression& Expression::operator=(Expression&&) noexcept = default;

Function::Function(Name function_name, std::vector<Parameter> function_parameters,
                   std::optional<TypeName> function_return_type,
                   std::unique_ptr<Block> function_body)
    : name{std::move(function_name)}, parameters{std::move(function_parameters)},
      return_type{std::move(function_return_type)}, body{std::move(function_body)} {}

Function::~Function() = default;
Function::Function(Function&&) noexcept = default;
Function& Function::operator=(Function&&) noexcept = default;

Statement::Statement(StatementKind statement_kind, const Span statement_span)
    : kind{std::move(statement_kind)}, span{statement_span} {}

Statement::~Statement() = default;
Statement::Statement(Statement&&) noexcept = default;
Statement& Statement::operator=(Statement&&) noexcept = default;

Block::Block(std::vector<Statement> block_statements, std::unique_ptr<Expression> tail_expression,
             const Span block_span)
    : statements{std::move(block_statements)}, tail{std::move(tail_expression)}, span{block_span} {}

Block::~Block() = default;
Block::Block(Block&&) noexcept = default;
Block& Block::operator=(Block&&) noexcept = default;

Program::Program(std::vector<Statement> program_statements, const Span program_span)
    : statements{std::move(program_statements)}, span{program_span} {}

Program::~Program() = default;
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;

namespace {

[[nodiscard]] std::string quoted(const std::string_view text) {
    constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string result{"\""};
    result.reserve(text.size() + 2U);

    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20U || byte == 0x7FU) {
                result += "\\x";
                result.push_back(hex_digits[(byte >> 4U) & 0x0FU]);
                result.push_back(hex_digits[byte & 0x0FU]);
            } else {
                result.push_back(character);
            }
            break;
        }
    }

    result.push_back('"');
    return result;
}

[[nodiscard]] std::string_view spelling(const UnaryOperator operator_kind) noexcept {
    switch (operator_kind) {
    case UnaryOperator::negate:
        return "-";
    case UnaryOperator::not_:
        return "!";
    }
    return "?";
}

[[nodiscard]] std::string_view spelling(const BinaryOperator operator_kind) noexcept {
    switch (operator_kind) {
    case BinaryOperator::add:
        return "+";
    case BinaryOperator::subtract:
        return "-";
    case BinaryOperator::multiply:
        return "*";
    case BinaryOperator::divide:
        return "/";
    case BinaryOperator::remainder:
        return "%";
    case BinaryOperator::equal:
        return "==";
    case BinaryOperator::not_equal:
        return "!=";
    case BinaryOperator::less:
        return "<";
    case BinaryOperator::less_equal:
        return "<=";
    case BinaryOperator::greater:
        return ">";
    case BinaryOperator::greater_equal:
        return ">=";
    }
    return "?";
}

class AstFormatter final {
  public:
    [[nodiscard]] std::string format(const Program& program) {
        line(0U, "Program", program.span);
        for (const auto& statement : program.statements) {
            format_statement(statement, 1U);
        }
        return std::move(output_);
    }

  private:
    void append_indent(const std::size_t depth) { output_.append(depth * 2U, ' '); }

    void append_span(const Span span) {
        output_ += " [";
        output_ += std::to_string(span.start);
        output_ += ", ";
        output_ += std::to_string(span.end);
        output_ += ")";
    }

    void line(const std::size_t depth, const std::string_view label, const Span span) {
        append_indent(depth);
        output_ += label;
        append_span(span);
        output_.push_back('\n');
    }

    void named_line(const std::size_t depth, const std::string_view label,
                    const std::string_view name, const Span span) {
        append_indent(depth);
        output_ += label;
        output_.push_back(' ');
        output_ += quoted(name);
        append_span(span);
        output_.push_back('\n');
    }

    void operator_line(const std::size_t depth, const std::string_view label,
                       const std::string_view operator_text, const Span expression_span,
                       const Span operator_span) {
        append_indent(depth);
        output_ += label;
        output_.push_back(' ');
        output_ += quoted(operator_text);
        append_span(expression_span);
        output_ += " operator=[";
        output_ += std::to_string(operator_span.start);
        output_ += ", ";
        output_ += std::to_string(operator_span.end);
        output_ += ")\n";
    }

    void format_type(const TypeName& type_name, const std::size_t depth) {
        named_line(depth, "TypeName", type_name.name.text, type_name.name.span);
    }

    void format_parameter(const Parameter& parameter, const std::size_t depth) {
        line(depth, "Parameter", parameter.span);
        named_line(depth + 1U, "Name", parameter.name.text, parameter.name.span);
        format_type(parameter.type_name, depth + 1U);
    }

    void format_function(const Function& function, const Span span, const std::size_t depth) {
        assert(function.body != nullptr);
        line(depth, "Function", span);
        named_line(depth + 1U, "Name", function.name.text, function.name.span);
        for (const auto& parameter : function.parameters) {
            format_parameter(parameter, depth + 1U);
        }
        if (function.return_type) {
            format_type(*function.return_type, depth + 1U);
        }
        format_block(*function.body, depth + 1U);
    }

    void format_statement(const Statement& statement, const std::size_t depth) {
        std::visit(
            [this, &statement, depth](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, Function>) {
                    format_function(node, statement.span, depth);
                } else if constexpr (std::is_same_v<Node, LetStatement>) {
                    append_indent(depth);
                    output_ += node.mutable_binding ? "Let mutable" : "Let immutable";
                    append_span(statement.span);
                    output_.push_back('\n');
                    named_line(depth + 1U, "Name", node.name.text, node.name.span);
                    if (node.type_annotation) {
                        format_type(*node.type_annotation, depth + 1U);
                    }
                    format_expression(node.initializer, depth + 1U);
                } else if constexpr (std::is_same_v<Node, ReturnStatement>) {
                    line(depth, "Return", statement.span);
                    if (node.value) {
                        format_expression(*node.value, depth + 1U);
                    }
                } else if constexpr (std::is_same_v<Node, ExpressionStatement>) {
                    line(depth, "ExpressionStatement", statement.span);
                    format_expression(node.expression, depth + 1U);
                }
            },
            statement.kind);
    }

    void format_block(const Block& block, const std::size_t depth) {
        line(depth, "Block", block.span);
        for (const auto& statement : block.statements) {
            format_statement(statement, depth + 1U);
        }
        if (block.tail) {
            format_expression(*block.tail, depth + 1U);
        }
    }

    void format_expression(const Expression& expression, const std::size_t depth) {
        std::visit(
            [this, &expression, depth](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, IntegerExpression>) {
                    append_indent(depth);
                    output_ += "Integer ";
                    output_ += std::to_string(node.value);
                    append_span(expression.span);
                    output_.push_back('\n');
                } else if constexpr (std::is_same_v<Node, StringExpression>) {
                    named_line(depth, "String", node.value, expression.span);
                } else if constexpr (std::is_same_v<Node, BooleanExpression>) {
                    append_indent(depth);
                    output_ += node.value ? "Boolean true" : "Boolean false";
                    append_span(expression.span);
                    output_.push_back('\n');
                } else if constexpr (std::is_same_v<Node, IdentifierExpression>) {
                    named_line(depth, "Identifier", node.name, expression.span);
                } else if constexpr (std::is_same_v<Node, GroupExpression>) {
                    assert(node.expression != nullptr);
                    line(depth, "Group", expression.span);
                    format_expression(*node.expression, depth + 1U);
                } else if constexpr (std::is_same_v<Node, BlockExpression>) {
                    assert(node.block != nullptr);
                    line(depth, "BlockExpression", expression.span);
                    format_block(*node.block, depth + 1U);
                } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
                    assert(node.operand != nullptr);
                    operator_line(depth, "Unary", spelling(node.operator_kind), expression.span,
                                  node.operator_span);
                    format_expression(*node.operand, depth + 1U);
                } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
                    assert(node.left != nullptr);
                    assert(node.right != nullptr);
                    operator_line(depth, "Binary", spelling(node.operator_kind), expression.span,
                                  node.operator_span);
                    format_expression(*node.left, depth + 1U);
                    format_expression(*node.right, depth + 1U);
                } else if constexpr (std::is_same_v<Node, AssignmentExpression>) {
                    assert(node.value != nullptr);
                    operator_line(depth, "Assignment", "=", expression.span, node.operator_span);
                    named_line(depth + 1U, "Name", node.name.text, node.name.span);
                    format_expression(*node.value, depth + 1U);
                } else if constexpr (std::is_same_v<Node, CallExpression>) {
                    assert(node.callee != nullptr);
                    line(depth, "Call", expression.span);
                    format_expression(*node.callee, depth + 1U);
                    for (const auto& argument : node.arguments) {
                        assert(argument != nullptr);
                        format_expression(*argument, depth + 1U);
                    }
                } else if constexpr (std::is_same_v<Node, IfExpression>) {
                    assert(node.condition != nullptr);
                    assert(node.then_branch != nullptr);
                    line(depth, "If", expression.span);
                    format_expression(*node.condition, depth + 1U);
                    format_block(*node.then_branch, depth + 1U);
                    if (node.else_branch) {
                        format_expression(*node.else_branch, depth + 1U);
                    }
                }
            },
            expression.kind);
    }

    std::string output_;
};

} // namespace

std::string format_ast(const Program& program) { return AstFormatter{}.format(program); }

} // namespace framestepp
