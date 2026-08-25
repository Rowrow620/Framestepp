#include "framestepp/lexer.hpp"
#include "framestepp/token.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

static_assert(!std::is_constructible_v<Lexer, std::string&&>);
static_assert(!std::is_constructible_v<Lexer, SourceFile&&>);
static_assert(std::is_constructible_v<Lexer, const char (&)[4]>);

[[nodiscard]] std::vector<Token> lex_ok(std::string text,
                                        const std::size_t token_limit = max_source_tokens) {
    const SourceFile source{"test.frame", std::move(text)};
    auto result = Lexer{source, token_limit}.lex();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&result)) {
        ADD_FAILURE() << "expected lexing to succeed, got: " << diagnostic->message;
        return {};
    }
    return std::move(std::get<std::vector<Token>>(result));
}

[[nodiscard]] Diagnostic lex_error(std::string text,
                                   const std::size_t token_limit = max_source_tokens) {
    const SourceFile source{"test.frame", std::move(text)};
    auto result = Lexer{source, token_limit}.lex();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&result)) {
        return *diagnostic;
    }
    ADD_FAILURE() << "expected lexing to fail";
    return Diagnostic{DiagnosticSeverity::error, "missing diagnostic", Span{}};
}

[[nodiscard]] std::vector<TokenKind> kinds_of(const std::vector<Token>& tokens) {
    std::vector<TokenKind> kinds;
    kinds.reserve(tokens.size());
    for (const auto& token : tokens) {
        kinds.push_back(token.kind);
    }
    return kinds;
}

TEST(LexerTest, LexesEveryOperatorAndDelimiter) {
    const auto tokens = lex_ok("(){}[],:;.+-*/%= == ! != < <= > >= -> |");

    EXPECT_EQ(kinds_of(tokens), (std::vector{
                                    TokenKind::left_parenthesis,
                                    TokenKind::right_parenthesis,
                                    TokenKind::left_brace,
                                    TokenKind::right_brace,
                                    TokenKind::left_bracket,
                                    TokenKind::right_bracket,
                                    TokenKind::comma,
                                    TokenKind::colon,
                                    TokenKind::semicolon,
                                    TokenKind::dot,
                                    TokenKind::plus,
                                    TokenKind::minus,
                                    TokenKind::star,
                                    TokenKind::slash,
                                    TokenKind::percent,
                                    TokenKind::equal,
                                    TokenKind::equal_equal,
                                    TokenKind::bang,
                                    TokenKind::bang_equal,
                                    TokenKind::less,
                                    TokenKind::less_equal,
                                    TokenKind::greater,
                                    TokenKind::greater_equal,
                                    TokenKind::arrow,
                                    TokenKind::pipe,
                                    TokenKind::end_of_file,
                                }));
}

TEST(LexerTest, RecognizesKeywordsWithoutStealingLongerIdentifiers) {
    const auto tokens =
        lex_ok("fn let mut if else match return true false frameout framein print letter");

    EXPECT_EQ(kinds_of(tokens), (std::vector{
                                    TokenKind::fn_keyword,
                                    TokenKind::let_keyword,
                                    TokenKind::mut_keyword,
                                    TokenKind::if_keyword,
                                    TokenKind::else_keyword,
                                    TokenKind::match_keyword,
                                    TokenKind::return_keyword,
                                    TokenKind::true_keyword,
                                    TokenKind::false_keyword,
                                    TokenKind::identifier,
                                    TokenKind::identifier,
                                    TokenKind::identifier,
                                    TokenKind::identifier,
                                    TokenKind::end_of_file,
                                }));
    ASSERT_EQ(tokens.size(), 14U);
    EXPECT_EQ(std::get<std::string>(tokens[9].value), "frameout");
    EXPECT_EQ(std::get<std::string>(tokens[12].value), "letter");
}

TEST(LexerTest, SkipsUnicodeWhitespaceAndLineComments) {
    const auto tokens = lex_ok("// caf\xC3\xA9\r\nlet\xC2\xA0// player name\nhero");

    ASSERT_EQ(kinds_of(tokens), (std::vector{
                                    TokenKind::let_keyword,
                                    TokenKind::identifier,
                                    TokenKind::end_of_file,
                                }));
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(std::get<std::string>(tokens[1].value), "hero");
}

TEST(LexerTest, EndsLineCommentsOnlyAtLineFeed) {
    const auto tokens = lex_ok("let // comment\rstill comment\nmut");

    EXPECT_EQ(kinds_of(tokens), (std::vector{
                                    TokenKind::let_keyword,
                                    TokenKind::mut_keyword,
                                    TokenKind::end_of_file,
                                }));
}

TEST(LexerTest, DecodesStringEscapesAndPreservesUtf8) {
    const std::string source{"\"Ada\\n\\t\\\"\\\\ caf\xC3\xA9\""};
    const auto tokens = lex_ok(source);

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::string);
    EXPECT_EQ(tokens[0].span, (Span{0, source.size()}));
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "Ada\n\t\"\\ caf\xC3\xA9");
    EXPECT_EQ(tokens[1].span, (Span{source.size(), source.size()}));
}

TEST(LexerTest, PreservesUnicodeIdentifiersAndByteSpans) {
    const auto tokens = lex_ok("let h\xC3\xA9ro = 1;");

    ASSERT_EQ(tokens.size(), 6U);
    EXPECT_EQ(tokens[1].kind, TokenKind::identifier);
    EXPECT_EQ(tokens[1].span, (Span{4, 9}));
    EXPECT_EQ(std::get<std::string>(tokens[1].value), "h\xC3\xA9ro");
    EXPECT_EQ(tokens[3].value, (TokenValue{std::int64_t{1}}));
    EXPECT_EQ(tokens.back().span, (Span{14, 14}));
}

TEST(LexerTest, AllowsUnicodeNumbersOnlyAfterAnIdentifierStart) {
    const auto tokens = lex_ok("a\xC2\xB2\xD9\xA3");

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::identifier);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "a\xC2\xB2\xD9\xA3");

    const auto error = lex_error("\xC2\xB2");
    EXPECT_EQ(error.span, (Span{0, 2}));
    EXPECT_EQ(error.message, "unexpected character '\xC2\xB2'");
}

TEST(LexerTest, RejectsMalformedUtf8AtTheInvalidByte) {
    const auto error = lex_error(std::string{"ok \x80", 4});

    EXPECT_EQ(error.message, "invalid UTF-8 encoding");
    EXPECT_EQ(error.span, (Span{3, 4}));
}

TEST(LexerTest, ReportsUnexpectedCharactersAndIntegerOverflow) {
    const auto character_error = lex_error("@");
    EXPECT_EQ(character_error.message, "unexpected character '@'");
    EXPECT_EQ(character_error.span, (Span{0, 1}));

    const auto integer_error = lex_error("999999999999999999999999");
    EXPECT_EQ(integer_error.message, "integer literal is too large");
    EXPECT_EQ(integer_error.span, (Span{0, 24}));
}

TEST(LexerTest, ReportsUnknownAndUnterminatedStringEscapes) {
    const auto unknown = lex_error("\"bad\\q\"");
    EXPECT_EQ(unknown.message, "unknown escape sequence after `\\`: 'q'");
    EXPECT_EQ(unknown.span, (Span{4, 6}));

    const auto escape = lex_error("\"bad\\");
    EXPECT_EQ(escape.message, "unterminated escape sequence");
    EXPECT_EQ(escape.span, (Span{4, 5}));

    const auto escaped_newline = lex_error("\"bad\\\n");
    EXPECT_EQ(escaped_newline.message, "unknown escape sequence after `\\`: U+000A");
    EXPECT_EQ(escaped_newline.span, (Span{4, 6}));
}

TEST(LexerTest, ReportsUnterminatedStringsAtEofAndNewline) {
    const auto eof = lex_error("\"unfinished");
    EXPECT_EQ(eof.message, "unterminated string literal");
    EXPECT_EQ(eof.span, (Span{0, 11}));

    const auto newline = lex_error("\"line\nnext");
    EXPECT_EQ(newline.message, "unterminated string literal");
    EXPECT_EQ(newline.span, (Span{0, 6}));
}

TEST(LexerTest, EnforcesTheDocumentedTokenLimitBeforeEof) {
    static_assert(max_source_tokens == 65'536);

    const auto error = lex_error("one two three", 2);
    EXPECT_EQ(error.message, "token limit exceeded (maximum 2)");
    EXPECT_EQ(error.span, (Span{8, 13}));

    const auto empty = lex_ok("   // no tokens", 0);
    ASSERT_EQ(empty.size(), 1U);
    EXPECT_EQ(empty[0].kind, TokenKind::end_of_file);
}

TEST(TokenTest, ProducesDeterministicDescriptions) {
    EXPECT_EQ(token_description(Token{TokenKind::identifier, Span{0, 4}, std::string{"hero"}}),
              "identifier \"hero\"");
    EXPECT_EQ(token_description(Token{TokenKind::integer, Span{0, 2}, std::int64_t{42}}),
              "integer 42");
    EXPECT_EQ(token_description(Token{TokenKind::string, Span{0, 5}, std::string{"a\n\""}}),
              "string \"a\\n\\\"\"");
    EXPECT_EQ(token_description(Token{TokenKind::plus, Span{0, 1}, {}}), "`+`");
    EXPECT_EQ(token_description(Token{}), "end of file");
}

} // namespace
} // namespace framestepp
