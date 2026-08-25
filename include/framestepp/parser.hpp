#pragma once

#include "framestepp/ast.hpp"
#include "framestepp/diagnostic.hpp"
#include "framestepp/token.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace framestepp {

inline constexpr std::size_t max_parse_depth = 64;
inline constexpr std::size_t max_expression_nodes = 512;

using ParseResult = std::variant<Program, Diagnostic>;

/// Converts an owned token stream into a move-only syntax tree.
class Parser final {
  public:
    explicit Parser(std::vector<Token> tokens);

    [[nodiscard]] ParseResult parse() &&;

  private:
    struct InfixBinding final {
        std::uint8_t left_power{0};
        std::uint8_t right_power{0};
        std::optional<BinaryOperator> binary_operator;
    };

    [[nodiscard]] std::optional<Statement> parse_statement();
    [[nodiscard]] std::optional<Statement> parse_function_statement();
    [[nodiscard]] std::optional<Statement> parse_let_statement();
    [[nodiscard]] std::optional<Statement> parse_return_statement();
    [[nodiscard]] std::optional<Statement> parse_expression_statement();
    [[nodiscard]] std::optional<Block> parse_block();
    [[nodiscard]] std::optional<Block> parse_block_after_open(Span start);

    [[nodiscard]] std::optional<Expression> parse_expression();
    [[nodiscard]] std::optional<Expression>
    parse_expression_with_binding_power(std::uint8_t minimum_power);
    [[nodiscard]] std::optional<Expression>
    parse_expression_with_binding_power_inner(std::uint8_t minimum_power);
    [[nodiscard]] std::optional<Expression> parse_prefix_expression();
    [[nodiscard]] std::optional<Expression> parse_if_expression(Span start);
    [[nodiscard]] std::optional<Expression> finish_call(Expression callee);
    [[nodiscard]] std::optional<InfixBinding> infix_binding() const noexcept;
    [[nodiscard]] std::optional<Expression> make_expression(ExpressionKind kind, Span span);

    [[nodiscard]] std::optional<TypeName> parse_type_name(std::string_view message);
    [[nodiscard]] std::optional<Name> expect_name(std::string_view message);

    [[nodiscard]] bool at(TokenKind expected) const noexcept;
    [[nodiscard]] bool take(TokenKind expected) noexcept;
    [[nodiscard]] const Token* take_token(TokenKind expected) noexcept;
    [[nodiscard]] const Token* expect(TokenKind expected, std::string_view message);
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& advance() noexcept;
    void fail(std::string_view message, Span span);

    std::vector<Token> tokens_;
    std::size_t cursor_{0};
    std::size_t parse_depth_{0};
    std::size_t expression_nodes_{0};
    std::optional<Diagnostic> diagnostic_;
};

} // namespace framestepp
