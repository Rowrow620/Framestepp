#include "framestepp/lexer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utf8proc.h>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

struct CodePoint final {
    utf8proc_int32_t value;
    std::size_t width;
};

[[nodiscard]] utf8proc_ssize_t iteration_length(const std::size_t remaining) noexcept {
    const auto maximum = static_cast<std::size_t>((std::numeric_limits<utf8proc_ssize_t>::max)());
    return static_cast<utf8proc_ssize_t>(std::min(remaining, maximum));
}

[[nodiscard]] CodePoint decode_valid(const std::string_view source,
                                     const std::size_t offset) noexcept {
    utf8proc_int32_t value = -1;
    const auto* bytes = reinterpret_cast<const utf8proc_uint8_t*>(source.data() + offset);
    const auto result = utf8proc_iterate(bytes, iteration_length(source.size() - offset), &value);

    // lex() validates the complete input before scanning, so this branch is
    // defensive and cannot be reached for an unchanged source view.
    if (result <= 0) {
        return CodePoint{0, 1};
    }
    return CodePoint{value, static_cast<std::size_t>(result)};
}

[[nodiscard]] bool is_letter_category(const utf8proc_int32_t value) noexcept {
    switch (utf8proc_category(value)) {
    case UTF8PROC_CATEGORY_LU:
    case UTF8PROC_CATEGORY_LL:
    case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM:
    case UTF8PROC_CATEGORY_LO:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_number_category(const utf8proc_int32_t value) noexcept {
    switch (utf8proc_category(value)) {
    case UTF8PROC_CATEGORY_ND:
    case UTF8PROC_CATEGORY_NL:
    case UTF8PROC_CATEGORY_NO:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_identifier_start(const utf8proc_int32_t value) noexcept {
    return value == '_' || is_letter_category(value);
}

[[nodiscard]] bool is_identifier_continue(const utf8proc_int32_t value) noexcept {
    return is_identifier_start(value) || is_number_category(value);
}

// Unicode 17 White_Space, written explicitly so behavior never depends on the
// process locale. This is the same fixed set used by Rust's char::is_whitespace.
[[nodiscard]] bool is_whitespace(const utf8proc_int32_t value) noexcept {
    return (value >= 0x0009 && value <= 0x000D) || value == 0x0020 || value == 0x0085 ||
           value == 0x00A0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200A) ||
           value == 0x2028 || value == 0x2029 || value == 0x202F || value == 0x205F ||
           value == 0x3000;
}

[[nodiscard]] std::optional<TokenKind> keyword_kind(const std::string_view text) noexcept {
    if (text == "fn") {
        return TokenKind::fn_keyword;
    }
    if (text == "let") {
        return TokenKind::let_keyword;
    }
    if (text == "mut") {
        return TokenKind::mut_keyword;
    }
    if (text == "if") {
        return TokenKind::if_keyword;
    }
    if (text == "else") {
        return TokenKind::else_keyword;
    }
    if (text == "match") {
        return TokenKind::match_keyword;
    }
    if (text == "return") {
        return TokenKind::return_keyword;
    }
    if (text == "true") {
        return TokenKind::true_keyword;
    }
    if (text == "false") {
        return TokenKind::false_keyword;
    }
    return std::nullopt;
}

[[nodiscard]] std::string code_point_label(const utf8proc_int32_t value,
                                           const std::string_view bytes) {
    if (utf8proc_category(value) != UTF8PROC_CATEGORY_CC && value >= 0x20 && value != 0x7F) {
        std::string label{"'"};
        if (value == '\\' || value == '\'') {
            label.push_back('\\');
        }
        label.append(bytes);
        label.push_back('\'');
        return label;
    }

    std::ostringstream label;
    label << "U+" << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << value;
    return label.str();
}

class Scanner final {
  public:
    Scanner(const std::string_view source, const std::size_t token_limit) noexcept
        : source_(source), token_limit_(token_limit) {}

    [[nodiscard]] LexResult run() {
        if (const auto invalid = validate_utf8()) {
            return *invalid;
        }

        std::vector<Token> tokens;
        while (cursor_ < source_.size()) {
            skip_trivia();
            if (cursor_ == source_.size()) {
                break;
            }

            auto result = next_token();
            if (auto* diagnostic = std::get_if<Diagnostic>(&result)) {
                return std::move(*diagnostic);
            }

            auto token = std::move(std::get<Token>(result));
            if (tokens.size() >= token_limit_) {
                return Diagnostic{
                    DiagnosticSeverity::error,
                    "token limit exceeded (maximum " + std::to_string(token_limit_) + ")",
                    token.span,
                };
            }
            tokens.push_back(std::move(token));
        }

        tokens.push_back(Token{TokenKind::end_of_file, Span{cursor_, cursor_}, {}});
        return tokens;
    }

  private:
    using TokenResult = std::variant<Token, Diagnostic>;

    [[nodiscard]] std::optional<Diagnostic> validate_utf8() const {
        std::size_t offset = 0;
        while (offset < source_.size()) {
            utf8proc_int32_t value = -1;
            const auto* bytes = reinterpret_cast<const utf8proc_uint8_t*>(source_.data() + offset);
            const auto width =
                utf8proc_iterate(bytes, iteration_length(source_.size() - offset), &value);
            if (width <= 0) {
                return Diagnostic{
                    DiagnosticSeverity::error,
                    "invalid UTF-8 encoding",
                    Span{offset, offset + 1},
                };
            }
            offset += static_cast<std::size_t>(width);
        }
        return std::nullopt;
    }

    void skip_trivia() noexcept {
        while (cursor_ < source_.size()) {
            const auto point = decode_valid(source_, cursor_);
            if (is_whitespace(point.value)) {
                cursor_ += point.width;
                continue;
            }

            if (source_[cursor_] != '/' || cursor_ + 1 >= source_.size() ||
                source_[cursor_ + 1] != '/') {
                break;
            }

            cursor_ += 2;
            while (cursor_ < source_.size()) {
                const auto comment_point = decode_valid(source_, cursor_);
                if (comment_point.value == '\n') {
                    break;
                }
                cursor_ += comment_point.width;
            }
        }
    }

    [[nodiscard]] TokenResult next_token() {
        const auto start = cursor_;
        const auto point = decode_valid(source_, cursor_);
        cursor_ += point.width;

        switch (point.value) {
        case '(':
            return plain_token(TokenKind::left_parenthesis, start);
        case ')':
            return plain_token(TokenKind::right_parenthesis, start);
        case '{':
            return plain_token(TokenKind::left_brace, start);
        case '}':
            return plain_token(TokenKind::right_brace, start);
        case '[':
            return plain_token(TokenKind::left_bracket, start);
        case ']':
            return plain_token(TokenKind::right_bracket, start);
        case ',':
            return plain_token(TokenKind::comma, start);
        case ':':
            return plain_token(TokenKind::colon, start);
        case ';':
            return plain_token(TokenKind::semicolon, start);
        case '.':
            return plain_token(TokenKind::dot, start);
        case '+':
            return plain_token(TokenKind::plus, start);
        case '*':
            return plain_token(TokenKind::star, start);
        case '/':
            return plain_token(TokenKind::slash, start);
        case '%':
            return plain_token(TokenKind::percent, start);
        case '|':
            return plain_token(TokenKind::pipe, start);
        case '-':
            return plain_token(take_ascii('>') ? TokenKind::arrow : TokenKind::minus, start);
        case '=':
            return plain_token(take_ascii('=') ? TokenKind::equal_equal : TokenKind::equal, start);
        case '!':
            return plain_token(take_ascii('=') ? TokenKind::bang_equal : TokenKind::bang, start);
        case '<':
            return plain_token(take_ascii('=') ? TokenKind::less_equal : TokenKind::less, start);
        case '>':
            return plain_token(take_ascii('=') ? TokenKind::greater_equal : TokenKind::greater,
                               start);
        case '"':
            return string_token(start);
        default:
            break;
        }

        if (point.value >= '0' && point.value <= '9') {
            return integer_token(start);
        }
        if (is_identifier_start(point.value)) {
            return identifier_token(start);
        }

        return Diagnostic{
            DiagnosticSeverity::error,
            "unexpected character " +
                code_point_label(point.value, source_.substr(start, point.width)),
            Span{start, cursor_},
        };
    }

    [[nodiscard]] Token plain_token(const TokenKind kind, const std::size_t start) const {
        return Token{kind, Span{start, cursor_}, {}};
    }

    [[nodiscard]] bool take_ascii(const char expected) noexcept {
        if (cursor_ < source_.size() && source_[cursor_] == expected) {
            ++cursor_;
            return true;
        }
        return false;
    }

    [[nodiscard]] Token identifier_token(const std::size_t start) {
        while (cursor_ < source_.size()) {
            const auto point = decode_valid(source_, cursor_);
            if (!is_identifier_continue(point.value)) {
                break;
            }
            cursor_ += point.width;
        }

        const auto text = source_.substr(start, cursor_ - start);
        if (const auto keyword = keyword_kind(text)) {
            return Token{*keyword, Span{start, cursor_}, {}};
        }
        return Token{TokenKind::identifier, Span{start, cursor_}, std::string{text}};
    }

    [[nodiscard]] TokenResult integer_token(const std::size_t start) {
        while (cursor_ < source_.size() && source_[cursor_] >= '0' && source_[cursor_] <= '9') {
            ++cursor_;
        }

        std::int64_t value = 0;
        const auto* first = source_.data() + start;
        const auto* last = source_.data() + cursor_;
        const auto conversion = std::from_chars(first, last, value);
        if (conversion.ec != std::errc{} || conversion.ptr != last) {
            return Diagnostic{
                DiagnosticSeverity::error,
                "integer literal is too large",
                Span{start, cursor_},
            };
        }
        return Token{TokenKind::integer, Span{start, cursor_}, value};
    }

    [[nodiscard]] TokenResult string_token(const std::size_t start) {
        std::string value;
        while (cursor_ < source_.size()) {
            const auto point = decode_valid(source_, cursor_);
            if (point.value == '"') {
                cursor_ += point.width;
                return Token{TokenKind::string, Span{start, cursor_}, std::move(value)};
            }
            if (point.value == '\n' || point.value == '\r') {
                cursor_ += point.width;
                return Diagnostic{
                    DiagnosticSeverity::error,
                    "unterminated string literal",
                    Span{start, cursor_},
                };
            }
            if (point.value != '\\') {
                value.append(source_.substr(cursor_, point.width));
                cursor_ += point.width;
                continue;
            }

            const auto escape_start = cursor_;
            cursor_ += point.width;
            if (cursor_ == source_.size()) {
                return Diagnostic{
                    DiagnosticSeverity::error,
                    "unterminated escape sequence",
                    Span{escape_start, cursor_},
                };
            }

            const auto escaped = decode_valid(source_, cursor_);
            cursor_ += escaped.width;
            switch (escaped.value) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            default:
                return Diagnostic{
                    DiagnosticSeverity::error,
                    "unknown escape sequence after `\\`: " +
                        code_point_label(escaped.value,
                                         source_.substr(cursor_ - escaped.width, escaped.width)),
                    Span{escape_start, cursor_},
                };
            }
        }

        return Diagnostic{
            DiagnosticSeverity::error,
            "unterminated string literal",
            Span{start, cursor_},
        };
    }

    std::string_view source_;
    std::size_t token_limit_;
    std::size_t cursor_{0};
};

} // namespace

Lexer::Lexer(const std::string_view source, const std::size_t token_limit) noexcept
    : source_(source), token_limit_(token_limit) {}

Lexer::Lexer(const SourceFile& source, const std::size_t token_limit) noexcept
    : Lexer(source.text(), token_limit) {}

LexResult Lexer::lex() const { return Scanner{source_, token_limit_}.run(); }

} // namespace framestepp
