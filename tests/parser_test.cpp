#include "framestepp/ast.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

static_assert(!std::is_copy_constructible_v<Expression>);
static_assert(!std::is_copy_constructible_v<Statement>);
static_assert(!std::is_copy_constructible_v<Program>);
static_assert(std::is_nothrow_move_constructible_v<Program>);
static_assert(std::is_invocable_v<decltype(&Parser::parse), Parser&&>);
static_assert(!std::is_invocable_v<decltype(&Parser::parse), Parser&>);
static_assert(max_parse_depth == 64U);
static_assert(max_expression_nodes == 512U);

[[nodiscard]] ParseResult parse_text(const std::string_view text) {
    auto lex_result = Lexer{text}.lex();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&lex_result)) {
        return *diagnostic;
    }
    return Parser{std::move(std::get<std::vector<Token>>(lex_result))}.parse();
}

[[nodiscard]] Program parse_ok(const std::string_view text) {
    auto result = parse_text(text);
    if (const auto* diagnostic = std::get_if<Diagnostic>(&result)) {
        ADD_FAILURE() << "expected parsing to succeed, got: " << diagnostic->message;
        return Program{{}, Span{}};
    }
    return std::move(std::get<Program>(result));
}

[[nodiscard]] Diagnostic parse_error(const std::string_view text) {
    auto result = parse_text(text);
    if (const auto* diagnostic = std::get_if<Diagnostic>(&result)) {
        return *diagnostic;
    }
    ADD_FAILURE() << "expected parsing to fail";
    return Diagnostic{DiagnosticSeverity::error, "missing diagnostic", Span{}};
}

[[nodiscard]] const LetStatement* first_let(const Program& program) {
    if (program.statements.empty()) {
        ADD_FAILURE() << "expected a statement";
        return nullptr;
    }
    const auto* statement = std::get_if<LetStatement>(&program.statements.front().kind);
    if (statement == nullptr) {
        ADD_FAILURE() << "expected a let statement";
    }
    return statement;
}

[[nodiscard]] std::string joined_addition(const std::size_t operand_count) {
    std::string expression;
    for (std::size_t index = 0; index < operand_count; ++index) {
        if (index != 0U) {
            expression += " + ";
        }
        expression += "1";
    }
    return expression;
}

TEST(ParserTest, ParsesEmptyAndNormalizesExternalTokenStreams) {
    auto empty_tokens = Parser{std::vector<Token>{}}.parse();
    ASSERT_TRUE(std::holds_alternative<Program>(empty_tokens));
    EXPECT_TRUE(std::get<Program>(empty_tokens).statements.empty());
    EXPECT_EQ(std::get<Program>(empty_tokens).span, (Span{0, 0}));

    auto program = parse_ok("");
    EXPECT_TRUE(program.statements.empty());
    EXPECT_EQ(program.span, (Span{0, 0}));
}

TEST(ParserTest, NormalizesEmbeddedEofAndRejectsMalformedExternalTokens) {
    std::vector<Token> embedded_eof{
        Token{TokenKind::integer, Span{0, 1}, std::int64_t{1}},
        Token{TokenKind::end_of_file, Span{1, 1}, {}},
        Token{TokenKind::semicolon, Span{1, 2}, {}},
    };
    auto normalized = Parser{std::move(embedded_eof)}.parse();
    ASSERT_TRUE(std::holds_alternative<Program>(normalized));
    EXPECT_EQ(std::get<Program>(normalized).span, (Span{0, 2}));

    std::vector<Token> malformed_payload{
        Token{TokenKind::identifier, Span{0, 4}, {}},
        Token{TokenKind::semicolon, Span{4, 5}, {}},
    };
    auto payload_result = Parser{std::move(malformed_payload)}.parse();
    ASSERT_TRUE(std::holds_alternative<Diagnostic>(payload_result));
    EXPECT_EQ(std::get<Diagnostic>(payload_result).message, "malformed identifier token");

    std::vector<Token> descending_spans{
        Token{TokenKind::integer, Span{2, 3}, std::int64_t{1}},
        Token{TokenKind::semicolon, Span{1, 2}, {}},
    };
    auto span_result = Parser{std::move(descending_spans)}.parse();
    ASSERT_TRUE(std::holds_alternative<Diagnostic>(span_result));
    EXPECT_EQ(std::get<Diagnostic>(span_result).message,
              "malformed token stream: invalid or out-of-order token span");
}

TEST(ParserTest, ParsesFunctionsParametersTypesAndReturns) {
    auto program =
        parse_ok("fn add(a: Int, b: Int,) -> Int { let mut total: Int = a + b; return total; }");

    ASSERT_EQ(program.statements.size(), 1U);
    const auto* function = std::get_if<Function>(&program.statements[0].kind);
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->name.text, "add");
    ASSERT_EQ(function->parameters.size(), 2U);
    EXPECT_EQ(function->parameters[0].name.text, "a");
    EXPECT_EQ(function->parameters[1].type_name.name.text, "Int");
    ASSERT_TRUE(function->return_type.has_value());
    EXPECT_EQ(function->return_type->name.text, "Int");
    ASSERT_NE(function->body, nullptr);
    ASSERT_EQ(function->body->statements.size(), 2U);

    const auto* binding = std::get_if<LetStatement>(&function->body->statements[0].kind);
    ASSERT_NE(binding, nullptr);
    EXPECT_TRUE(binding->mutable_binding);
    ASSERT_TRUE(binding->type_annotation.has_value());
    EXPECT_EQ(binding->type_annotation->name.text, "Int");

    const auto* return_statement =
        std::get_if<ReturnStatement>(&function->body->statements[1].kind);
    ASSERT_NE(return_statement, nullptr);
    EXPECT_TRUE(return_statement->value.has_value());
    EXPECT_EQ(program.statements[0].span, program.span);
}

TEST(ParserTest, ParsesBareReturnsAndFunctionsWithoutReturnAnnotations) {
    auto program = parse_ok("fn noop() { return; }");

    const auto* function = std::get_if<Function>(&program.statements[0].kind);
    ASSERT_NE(function, nullptr);
    EXPECT_FALSE(function->return_type.has_value());
    ASSERT_EQ(function->body->statements.size(), 1U);
    const auto* return_statement =
        std::get_if<ReturnStatement>(&function->body->statements[0].kind);
    ASSERT_NE(return_statement, nullptr);
    EXPECT_FALSE(return_statement->value.has_value());
}

TEST(ParserTest, AppliesCallUnaryAndBinaryPrecedence) {
    auto program = parse_ok("let value = (1 + 2) * -calculate(3) == 0;");
    const LetStatement* binding = first_let(program);
    ASSERT_NE(binding, nullptr);

    const auto* equality = std::get_if<BinaryExpression>(&binding->initializer.kind);
    ASSERT_NE(equality, nullptr);
    EXPECT_EQ(equality->operator_kind, BinaryOperator::equal);
    const auto* multiplication = std::get_if<BinaryExpression>(&equality->left->kind);
    ASSERT_NE(multiplication, nullptr);
    EXPECT_EQ(multiplication->operator_kind, BinaryOperator::multiply);
    EXPECT_TRUE(std::holds_alternative<GroupExpression>(multiplication->left->kind));
    const auto* unary = std::get_if<UnaryExpression>(&multiplication->right->kind);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->operator_kind, UnaryOperator::negate);
    EXPECT_TRUE(std::holds_alternative<CallExpression>(unary->operand->kind));
}

TEST(ParserTest, ParsesChainedCallsAndTrailingArgumentCommas) {
    auto program = parse_ok("factory()(1, 2,);");
    const auto* statement = std::get_if<ExpressionStatement>(&program.statements[0].kind);
    ASSERT_NE(statement, nullptr);
    const auto* outer_call = std::get_if<CallExpression>(&statement->expression.kind);
    ASSERT_NE(outer_call, nullptr);
    EXPECT_EQ(outer_call->arguments.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<CallExpression>(outer_call->callee->kind));
}

TEST(ParserTest, DistinguishesBlockStatementsFromTailExpressions) {
    auto program = parse_ok("fn calculate() -> Int { let x = 1; frameout(x); x + 1 }");
    const auto* function = std::get_if<Function>(&program.statements[0].kind);
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);
    EXPECT_EQ(function->body->statements.size(), 2U);
    ASSERT_NE(function->body->tail, nullptr);
    const auto* tail = std::get_if<BinaryExpression>(&function->body->tail->kind);
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(tail->operator_kind, BinaryOperator::add);

    auto block_value = parse_ok("let value = { frameout(1); 2 }; ");
    const LetStatement* binding = first_let(block_value);
    ASSERT_NE(binding, nullptr);
    const auto* block_expression = std::get_if<BlockExpression>(&binding->initializer.kind);
    ASSERT_NE(block_expression, nullptr);
    EXPECT_EQ(block_expression->block->statements.size(), 1U);
    EXPECT_NE(block_expression->block->tail, nullptr);
}

TEST(ParserTest, ParsesIfElseAndElseIfAsExpressions) {
    auto program = parse_ok("let sign = if x > 0 { 1 } else if x < 0 { -1 } else { 0 };");
    const LetStatement* binding = first_let(program);
    ASSERT_NE(binding, nullptr);
    const auto* outer_if = std::get_if<IfExpression>(&binding->initializer.kind);
    ASSERT_NE(outer_if, nullptr);
    ASSERT_NE(outer_if->then_branch->tail, nullptr);
    ASSERT_NE(outer_if->else_branch, nullptr);
    const auto* inner_if = std::get_if<IfExpression>(&outer_if->else_branch->kind);
    ASSERT_NE(inner_if, nullptr);
    ASSERT_NE(inner_if->else_branch, nullptr);
    EXPECT_TRUE(std::holds_alternative<BlockExpression>(inner_if->else_branch->kind));
}

TEST(ParserTest, MakesAssignmentRightAssociativeAndPreservesTargetSpans) {
    auto program = parse_ok("left = right = 1;");
    const auto* statement = std::get_if<ExpressionStatement>(&program.statements[0].kind);
    ASSERT_NE(statement, nullptr);
    const auto* outer = std::get_if<AssignmentExpression>(&statement->expression.kind);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->name.text, "left");
    EXPECT_EQ(outer->name.span, (Span{0, 4}));
    EXPECT_EQ(outer->operator_span, (Span{5, 6}));
    const auto* inner = std::get_if<AssignmentExpression>(&outer->value->kind);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->name.text, "right");
    EXPECT_EQ(inner->name.span, (Span{7, 12}));
}

TEST(ParserTest, RejectsInvalidAssignmentTargets) {
    const auto error = parse_error("(left + right) = 1;");
    EXPECT_EQ(error.message, "invalid assignment target; expected a variable name");
    EXPECT_EQ(error.span, (Span{0, 14}));
}

TEST(ParserTest, ReportsStableGrammarErrorsAtTheCurrentToken) {
    struct ErrorCase final {
        std::string_view source;
        std::string_view message;
    };
    const std::vector<ErrorCase> cases{
        {"fn broken(value: Int -> Int { value }", "expected `)` after function parameters"},
        {"let value = 1", "expected `;` after variable declaration"},
        {"fn broken() { let value = 1;", "expected `}` to close block"},
        {"fn outer() { fn inner() {} }", "function declarations are only allowed at top level"},
        {"let value = 1 + * 2;", "expected an expression, found `*`"},
        {"fn broken() { 1 2 }", "expected `;` or `}` after expression"},
    };

    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.source);
        const auto error = parse_error(test_case.source);
        EXPECT_EQ(error.message, test_case.message);
        EXPECT_TRUE(error.span.is_valid());
    }
}

TEST(ParserTest, EnforcesTheActivePrattCallLimit) {
    const std::string accepted = "let value = " + std::string(max_parse_depth - 1U, '!') + "true;";
    EXPECT_TRUE(std::holds_alternative<Program>(parse_text(accepted)));

    const std::string rejected = "let value = " + std::string(max_parse_depth, '!') + "true;";
    const auto error = parse_error(rejected);
    EXPECT_EQ(error.message, "expression nesting limit exceeded");
}

TEST(ParserTest, AppliesTheNestingLimitToElseIfChains) {
    std::string expression{"{ 0 }"};
    for (std::size_t depth = 0; depth <= max_parse_depth; ++depth) {
        expression = "if false { 0 } else " + expression;
    }

    const auto error = parse_error("let value = " + expression + ";");
    EXPECT_EQ(error.message, "expression nesting limit exceeded");
}

TEST(ParserTest, EnforcesTheExpressionNodeLimitPerRoot) {
    // 256 leaves + 255 binary nodes + one group node reaches the exact limit.
    const std::string accepted = "let value = (" + joined_addition(256U) + ");";
    EXPECT_TRUE(std::holds_alternative<Program>(parse_text(accepted)));

    // A flat expression with 257 leaves needs 513 syntax nodes.
    const std::string rejected = "let value = " + joined_addition(257U) + ";";
    const auto error = parse_error(rejected);
    EXPECT_EQ(error.message, "expression exceeds the limit of 512 syntax nodes");

    const std::string two_roots = accepted + accepted;
    EXPECT_TRUE(std::holds_alternative<Program>(parse_text(two_roots)));
}

TEST(AstFormatterTest, ProducesStablePreorderOutputWithSpansAndEscapes) {
    auto program = parse_ok("let text = \"a\\n\\\"\";");

    EXPECT_EQ(format_ast(program), "Program [0, 19)\n"
                                   "  Let immutable [0, 19)\n"
                                   "    Name \"text\" [4, 8)\n"
                                   "    String \"a\\n\\\"\" [11, 18)\n");
    EXPECT_EQ(format_ast(program), format_ast(program));
}

} // namespace
} // namespace framestepp
