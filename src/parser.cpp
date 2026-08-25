#include "framestepp/parser.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace framestepp {
namespace {

[[nodiscard]] constexpr Span through(const Span first, const Span last) noexcept {
    return Span{first.start, last.end};
}

[[nodiscard]] std::string parser_token_description(const Token& token) {
    if (token.kind == TokenKind::identifier) {
        if (const auto* name = std::get_if<std::string>(&token.value)) {
            return "identifier \"" + *name + "\"";
        }
        return "an identifier";
    }
    if (token.kind == TokenKind::integer) {
        if (const auto* value = std::get_if<std::int64_t>(&token.value)) {
            return "integer `" + std::to_string(*value) + "`";
        }
        return "an integer";
    }
    if (token.kind == TokenKind::string) {
        return "a string";
    }
    if (token.kind == TokenKind::end_of_file) {
        return "the end of the file";
    }
    return "`" + std::string{token_kind_name(token.kind)} + "`";
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_{std::move(tokens)} {
    std::size_t end = 0U;
    for (const auto& token : tokens_) {
        end = std::max({end, token.span.start, token.span.end});
    }
    std::erase_if(tokens_, [](const Token& token) { return token.kind == TokenKind::end_of_file; });

    std::size_t previous_end = 0U;
    bool first_token = true;
    for (const auto& token : tokens_) {
        if (!token.span.is_valid() || (!first_token && token.span.start < previous_end)) {
            diagnostic_.emplace(DiagnosticSeverity::error,
                                "malformed token stream: invalid or out-of-order token span",
                                Span{std::min(token.span.start, token.span.end),
                                     std::max(token.span.start, token.span.end)});
            break;
        }
        previous_end = token.span.end;
        first_token = false;
    }
    tokens_.push_back(Token{TokenKind::end_of_file, Span{end, end}, {}});
}

ParseResult Parser::parse() && {
    if (diagnostic_) {
        return *diagnostic_;
    }

    const std::size_t start = current().span.start;
    std::vector<Statement> statements;

    while (!at(TokenKind::end_of_file)) {
        auto statement = parse_statement();
        if (!statement) {
            return *diagnostic_;
        }
        statements.push_back(std::move(*statement));
    }

    return Program{std::move(statements), Span{start, current().span.end}};
}

std::optional<Statement> Parser::parse_statement() {
    if (at(TokenKind::fn_keyword)) {
        return parse_function_statement();
    }
    if (at(TokenKind::let_keyword)) {
        return parse_let_statement();
    }
    if (at(TokenKind::return_keyword)) {
        return parse_return_statement();
    }
    return parse_expression_statement();
}

std::optional<Statement> Parser::parse_function_statement() {
    const Token* start = expect(TokenKind::fn_keyword, "expected `fn`");
    if (start == nullptr) {
        return std::nullopt;
    }
    auto name = expect_name("expected a function name after `fn`");
    if (!name) {
        return std::nullopt;
    }
    if (expect(TokenKind::left_parenthesis, "expected `(` after the function name") == nullptr) {
        return std::nullopt;
    }

    std::vector<Parameter> parameters;
    if (!at(TokenKind::right_parenthesis)) {
        while (true) {
            auto parameter_name = expect_name("expected a parameter name");
            if (!parameter_name) {
                return std::nullopt;
            }
            if (expect(TokenKind::colon, "expected `:` after the parameter name") == nullptr) {
                return std::nullopt;
            }
            auto type_name = parse_type_name("expected a parameter type after `:`");
            if (!type_name) {
                return std::nullopt;
            }

            const Span parameter_span = through(parameter_name->span, type_name->name.span);
            parameters.push_back(
                Parameter{std::move(*parameter_name), std::move(*type_name), parameter_span});
            if (!take(TokenKind::comma)) {
                break;
            }
            if (at(TokenKind::right_parenthesis)) {
                break;
            }
        }
    }
    if (expect(TokenKind::right_parenthesis, "expected `)` after function parameters") == nullptr) {
        return std::nullopt;
    }

    std::optional<TypeName> return_type;
    if (take(TokenKind::arrow)) {
        auto parsed_type = parse_type_name("expected a return type after `->`");
        if (!parsed_type) {
            return std::nullopt;
        }
        return_type.emplace(std::move(*parsed_type));
    }

    auto body = parse_block();
    if (!body) {
        return std::nullopt;
    }
    const Span statement_span = through(start->span, body->span);
    auto function = Function{std::move(*name), std::move(parameters), std::move(return_type),
                             std::make_unique<Block>(std::move(*body))};
    return Statement{StatementKind{std::move(function)}, statement_span};
}

std::optional<Statement> Parser::parse_let_statement() {
    const Token* start = expect(TokenKind::let_keyword, "expected `let`");
    if (start == nullptr) {
        return std::nullopt;
    }
    const bool mutable_binding = take(TokenKind::mut_keyword);
    auto name = expect_name("expected a variable name after `let`");
    if (!name) {
        return std::nullopt;
    }

    std::optional<TypeName> type_annotation;
    if (take(TokenKind::colon)) {
        auto parsed_type = parse_type_name("expected a type after `:`");
        if (!parsed_type) {
            return std::nullopt;
        }
        type_annotation.emplace(std::move(*parsed_type));
    }
    if (expect(TokenKind::equal, "expected `=` in variable declaration") == nullptr) {
        return std::nullopt;
    }
    auto initializer = parse_expression();
    if (!initializer) {
        return std::nullopt;
    }
    const Token* end = expect(TokenKind::semicolon, "expected `;` after variable declaration");
    if (end == nullptr) {
        return std::nullopt;
    }

    auto let_statement = LetStatement{mutable_binding, std::move(*name), std::move(type_annotation),
                                      std::move(*initializer)};
    return Statement{StatementKind{std::move(let_statement)}, through(start->span, end->span)};
}

std::optional<Statement> Parser::parse_return_statement() {
    const Token* start = expect(TokenKind::return_keyword, "expected `return`");
    if (start == nullptr) {
        return std::nullopt;
    }

    std::optional<Expression> value;
    if (!at(TokenKind::semicolon)) {
        auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        value.emplace(std::move(*expression));
    }
    const Token* end = expect(TokenKind::semicolon, "expected `;` after return value");
    if (end == nullptr) {
        return std::nullopt;
    }

    return Statement{StatementKind{ReturnStatement{std::move(value)}},
                     through(start->span, end->span)};
}

std::optional<Statement> Parser::parse_expression_statement() {
    auto expression = parse_expression();
    if (!expression) {
        return std::nullopt;
    }
    const Token* end = expect(TokenKind::semicolon, "expected `;` after expression");
    if (end == nullptr) {
        return std::nullopt;
    }
    const Span statement_span = through(expression->span, end->span);
    return Statement{StatementKind{ExpressionStatement{std::move(*expression)}}, statement_span};
}

std::optional<Block> Parser::parse_block() {
    const Token* start = expect(TokenKind::left_brace, "expected `{` to start a block");
    if (start == nullptr) {
        return std::nullopt;
    }
    return parse_block_after_open(start->span);
}

std::optional<Block> Parser::parse_block_after_open(const Span start) {
    std::vector<Statement> statements;
    std::unique_ptr<Expression> tail;

    while (!at(TokenKind::right_brace) && !at(TokenKind::end_of_file)) {
        if (at(TokenKind::fn_keyword)) {
            fail("function declarations are only allowed at top level", current().span);
            return std::nullopt;
        }
        if (at(TokenKind::let_keyword)) {
            auto statement = parse_let_statement();
            if (!statement) {
                return std::nullopt;
            }
            statements.push_back(std::move(*statement));
            continue;
        }
        if (at(TokenKind::return_keyword)) {
            auto statement = parse_return_statement();
            if (!statement) {
                return std::nullopt;
            }
            statements.push_back(std::move(*statement));
            continue;
        }

        auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        if (const Token* semicolon = take_token(TokenKind::semicolon)) {
            const Span statement_span = through(expression->span, semicolon->span);
            statements.emplace_back(StatementKind{ExpressionStatement{std::move(*expression)}},
                                    statement_span);
        } else if (at(TokenKind::right_brace) || at(TokenKind::end_of_file)) {
            tail = std::make_unique<Expression>(std::move(*expression));
            break;
        } else {
            fail("expected `;` or `}` after expression", current().span);
            return std::nullopt;
        }
    }

    const Token* end = expect(TokenKind::right_brace, "expected `}` to close block");
    if (end == nullptr) {
        return std::nullopt;
    }
    return Block{std::move(statements), std::move(tail), through(start, end->span)};
}

std::optional<Expression> Parser::parse_expression() {
    return parse_expression_with_binding_power(0U);
}

std::optional<Expression>
Parser::parse_expression_with_binding_power(const std::uint8_t minimum_power) {
    if (parse_depth_ == 0U) {
        expression_nodes_ = 0U;
    }
    if (parse_depth_ >= max_parse_depth) {
        fail("expression nesting limit exceeded", current().span);
        return std::nullopt;
    }

    ++parse_depth_;
    auto expression = parse_expression_with_binding_power_inner(minimum_power);
    --parse_depth_;
    return expression;
}

std::optional<Expression>
Parser::parse_expression_with_binding_power_inner(const std::uint8_t minimum_power) {
    auto left_result = parse_prefix_expression();
    if (!left_result) {
        return std::nullopt;
    }
    Expression left = std::move(*left_result);

    while (true) {
        constexpr std::uint8_t call_binding_power = 11U;
        if (at(TokenKind::left_parenthesis)) {
            if (call_binding_power < minimum_power) {
                break;
            }
            auto call = finish_call(std::move(left));
            if (!call) {
                return std::nullopt;
            }
            left = std::move(*call);
            continue;
        }

        const auto binding = infix_binding();
        if (!binding || binding->left_power < minimum_power) {
            break;
        }

        const Token& operator_token = advance();
        auto right = parse_expression_with_binding_power(binding->right_power);
        if (!right) {
            return std::nullopt;
        }
        const Span expression_span = through(left.span, right->span);

        if (operator_token.kind == TokenKind::equal) {
            auto* identifier = std::get_if<IdentifierExpression>(&left.kind);
            if (identifier == nullptr) {
                fail("invalid assignment target; expected a variable name", left.span);
                return std::nullopt;
            }
            Name name{std::move(identifier->name), left.span};
            auto assignment = AssignmentExpression{std::move(name), operator_token.span,
                                                   std::make_unique<Expression>(std::move(*right))};
            auto expression =
                make_expression(ExpressionKind{std::move(assignment)}, expression_span);
            if (!expression) {
                return std::nullopt;
            }
            left = std::move(*expression);
            continue;
        }

        auto binary = BinaryExpression{std::make_unique<Expression>(std::move(left)),
                                       *binding->binary_operator, operator_token.span,
                                       std::make_unique<Expression>(std::move(*right))};
        auto expression = make_expression(ExpressionKind{std::move(binary)}, expression_span);
        if (!expression) {
            return std::nullopt;
        }
        left = std::move(*expression);
    }

    return left;
}

std::optional<Expression> Parser::parse_prefix_expression() {
    const Token& token = advance();
    switch (token.kind) {
    case TokenKind::integer: {
        const auto* value = std::get_if<std::int64_t>(&token.value);
        if (value == nullptr) {
            fail("malformed integer token", token.span);
            return std::nullopt;
        }
        return make_expression(ExpressionKind{IntegerExpression{*value}}, token.span);
    }
    case TokenKind::string: {
        const auto* value = std::get_if<std::string>(&token.value);
        if (value == nullptr) {
            fail("malformed string token", token.span);
            return std::nullopt;
        }
        return make_expression(ExpressionKind{StringExpression{*value}}, token.span);
    }
    case TokenKind::true_keyword:
    case TokenKind::false_keyword:
        return make_expression(
            ExpressionKind{BooleanExpression{token.kind == TokenKind::true_keyword}}, token.span);
    case TokenKind::identifier: {
        const auto* name = std::get_if<std::string>(&token.value);
        if (name == nullptr) {
            fail("malformed identifier token", token.span);
            return std::nullopt;
        }
        return make_expression(ExpressionKind{IdentifierExpression{*name}}, token.span);
    }
    case TokenKind::minus:
    case TokenKind::bang: {
        const UnaryOperator operator_kind =
            token.kind == TokenKind::minus ? UnaryOperator::negate : UnaryOperator::not_;
        auto operand = parse_expression_with_binding_power(10U);
        if (!operand) {
            return std::nullopt;
        }
        const Span expression_span = through(token.span, operand->span);
        auto unary = UnaryExpression{operator_kind, token.span,
                                     std::make_unique<Expression>(std::move(*operand))};
        return make_expression(ExpressionKind{std::move(unary)}, expression_span);
    }
    case TokenKind::left_parenthesis: {
        auto inner = parse_expression();
        if (!inner) {
            return std::nullopt;
        }
        const Token* end = expect(TokenKind::right_parenthesis, "expected `)` after expression");
        if (end == nullptr) {
            return std::nullopt;
        }
        auto group = GroupExpression{std::make_unique<Expression>(std::move(*inner))};
        return make_expression(ExpressionKind{std::move(group)}, through(token.span, end->span));
    }
    case TokenKind::left_brace: {
        auto block = parse_block_after_open(token.span);
        if (!block) {
            return std::nullopt;
        }
        const Span expression_span = block->span;
        auto block_expression = BlockExpression{std::make_unique<Block>(std::move(*block))};
        return make_expression(ExpressionKind{std::move(block_expression)}, expression_span);
    }
    case TokenKind::if_keyword:
        return parse_if_expression(token.span);
    default:
        fail("expected an expression, found " + parser_token_description(token), token.span);
        return std::nullopt;
    }
}

std::optional<Expression> Parser::parse_if_expression(const Span start) {
    auto condition = parse_expression();
    if (!condition) {
        return std::nullopt;
    }
    auto then_branch = parse_block();
    if (!then_branch) {
        return std::nullopt;
    }

    std::unique_ptr<Expression> else_branch;
    if (take(TokenKind::else_keyword)) {
        if (at(TokenKind::if_keyword)) {
            auto nested_if = parse_expression_with_binding_power(0U);
            if (!nested_if) {
                return std::nullopt;
            }
            else_branch = std::make_unique<Expression>(std::move(*nested_if));
        } else {
            auto else_block = parse_block();
            if (!else_block) {
                return std::nullopt;
            }
            const Span block_span = else_block->span;
            auto block_expression =
                BlockExpression{std::make_unique<Block>(std::move(*else_block))};
            auto expression =
                make_expression(ExpressionKind{std::move(block_expression)}, block_span);
            if (!expression) {
                return std::nullopt;
            }
            else_branch = std::make_unique<Expression>(std::move(*expression));
        }
    }

    const Span end_span = else_branch ? else_branch->span : then_branch->span;
    auto if_expression =
        IfExpression{std::make_unique<Expression>(std::move(*condition)),
                     std::make_unique<Block>(std::move(*then_branch)), std::move(else_branch)};
    return make_expression(ExpressionKind{std::move(if_expression)}, through(start, end_span));
}

std::optional<Expression> Parser::finish_call(Expression callee) {
    if (expect(TokenKind::left_parenthesis, "expected `(`") == nullptr) {
        return std::nullopt;
    }
    std::vector<std::unique_ptr<Expression>> arguments;
    if (!at(TokenKind::right_parenthesis)) {
        while (true) {
            auto argument = parse_expression();
            if (!argument) {
                return std::nullopt;
            }
            arguments.push_back(std::make_unique<Expression>(std::move(*argument)));
            if (!take(TokenKind::comma)) {
                break;
            }
            if (at(TokenKind::right_parenthesis)) {
                break;
            }
        }
    }
    const Token* end =
        expect(TokenKind::right_parenthesis, "expected `)` after function arguments");
    if (end == nullptr) {
        return std::nullopt;
    }

    const Span expression_span = through(callee.span, end->span);
    auto call =
        CallExpression{std::make_unique<Expression>(std::move(callee)), std::move(arguments)};
    return make_expression(ExpressionKind{std::move(call)}, expression_span);
}

std::optional<Parser::InfixBinding> Parser::infix_binding() const noexcept {
    switch (current().kind) {
    case TokenKind::equal:
        return InfixBinding{1U, 1U, std::nullopt};
    case TokenKind::equal_equal:
        return InfixBinding{2U, 3U, BinaryOperator::equal};
    case TokenKind::bang_equal:
        return InfixBinding{2U, 3U, BinaryOperator::not_equal};
    case TokenKind::less:
        return InfixBinding{4U, 5U, BinaryOperator::less};
    case TokenKind::less_equal:
        return InfixBinding{4U, 5U, BinaryOperator::less_equal};
    case TokenKind::greater:
        return InfixBinding{4U, 5U, BinaryOperator::greater};
    case TokenKind::greater_equal:
        return InfixBinding{4U, 5U, BinaryOperator::greater_equal};
    case TokenKind::plus:
        return InfixBinding{6U, 7U, BinaryOperator::add};
    case TokenKind::minus:
        return InfixBinding{6U, 7U, BinaryOperator::subtract};
    case TokenKind::star:
        return InfixBinding{8U, 9U, BinaryOperator::multiply};
    case TokenKind::slash:
        return InfixBinding{8U, 9U, BinaryOperator::divide};
    case TokenKind::percent:
        return InfixBinding{8U, 9U, BinaryOperator::remainder};
    default:
        return std::nullopt;
    }
}

std::optional<Expression> Parser::make_expression(ExpressionKind kind, const Span span) {
    if (expression_nodes_ >= max_expression_nodes) {
        fail("expression exceeds the limit of " + std::to_string(max_expression_nodes) +
                 " syntax nodes",
             span);
        return std::nullopt;
    }
    ++expression_nodes_;
    return Expression{std::move(kind), span};
}

std::optional<TypeName> Parser::parse_type_name(const std::string_view message) {
    auto name = expect_name(message);
    if (!name) {
        return std::nullopt;
    }
    return TypeName{std::move(*name)};
}

std::optional<Name> Parser::expect_name(const std::string_view message) {
    const Token& token = advance();
    if (token.kind != TokenKind::identifier) {
        fail(message, token.span);
        return std::nullopt;
    }
    const auto* name = std::get_if<std::string>(&token.value);
    if (name == nullptr) {
        fail("malformed identifier token", token.span);
        return std::nullopt;
    }
    return Name{*name, token.span};
}

bool Parser::at(const TokenKind expected) const noexcept { return current().kind == expected; }

bool Parser::take(const TokenKind expected) noexcept { return take_token(expected) != nullptr; }

const Token* Parser::take_token(const TokenKind expected) noexcept {
    if (!at(expected)) {
        return nullptr;
    }
    return &advance();
}

const Token* Parser::expect(const TokenKind expected, const std::string_view message) {
    if (!at(expected)) {
        fail(message, current().span);
        return nullptr;
    }
    return &advance();
}

const Token& Parser::current() const noexcept { return tokens_[cursor_]; }

const Token& Parser::advance() noexcept {
    const Token& token = current();
    if (token.kind != TokenKind::end_of_file) {
        ++cursor_;
    }
    return token;
}

void Parser::fail(const std::string_view message, const Span span) {
    if (!diagnostic_) {
        diagnostic_.emplace(DiagnosticSeverity::error, std::string{message}, span);
    }
}

} // namespace framestepp
