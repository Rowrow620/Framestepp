#include "framestepp/token.hpp"

#include <string>

namespace framestepp {
namespace {

[[nodiscard]] std::string quoted(const std::string_view text) {
    constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string result{"\""};
    result.reserve(text.size() + 2);

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

} // namespace

std::string_view token_kind_name(const TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::identifier:
        return "identifier";
    case TokenKind::integer:
        return "integer";
    case TokenKind::string:
        return "string";
    case TokenKind::fn_keyword:
        return "fn";
    case TokenKind::let_keyword:
        return "let";
    case TokenKind::mut_keyword:
        return "mut";
    case TokenKind::if_keyword:
        return "if";
    case TokenKind::else_keyword:
        return "else";
    case TokenKind::match_keyword:
        return "match";
    case TokenKind::return_keyword:
        return "return";
    case TokenKind::true_keyword:
        return "true";
    case TokenKind::false_keyword:
        return "false";
    case TokenKind::left_parenthesis:
        return "(";
    case TokenKind::right_parenthesis:
        return ")";
    case TokenKind::left_brace:
        return "{";
    case TokenKind::right_brace:
        return "}";
    case TokenKind::left_bracket:
        return "[";
    case TokenKind::right_bracket:
        return "]";
    case TokenKind::comma:
        return ",";
    case TokenKind::colon:
        return ":";
    case TokenKind::semicolon:
        return ";";
    case TokenKind::dot:
        return ".";
    case TokenKind::plus:
        return "+";
    case TokenKind::minus:
        return "-";
    case TokenKind::star:
        return "*";
    case TokenKind::slash:
        return "/";
    case TokenKind::percent:
        return "%";
    case TokenKind::equal:
        return "=";
    case TokenKind::equal_equal:
        return "==";
    case TokenKind::bang:
        return "!";
    case TokenKind::bang_equal:
        return "!=";
    case TokenKind::less:
        return "<";
    case TokenKind::less_equal:
        return "<=";
    case TokenKind::greater:
        return ">";
    case TokenKind::greater_equal:
        return ">=";
    case TokenKind::arrow:
        return "->";
    case TokenKind::pipe:
        return "|";
    case TokenKind::end_of_file:
        return "end of file";
    }
    return "unknown token";
}

std::string token_description(const Token& token) {
    if (token.kind == TokenKind::identifier) {
        if (const auto* text = std::get_if<std::string>(&token.value)) {
            return "identifier " + quoted(*text);
        }
    } else if (token.kind == TokenKind::integer) {
        if (const auto* value = std::get_if<std::int64_t>(&token.value)) {
            return "integer " + std::to_string(*value);
        }
    } else if (token.kind == TokenKind::string) {
        if (const auto* text = std::get_if<std::string>(&token.value)) {
            return "string " + quoted(*text);
        }
    } else if (token.kind == TokenKind::end_of_file) {
        return std::string{token_kind_name(token.kind)};
    }

    return "`" + std::string{token_kind_name(token.kind)} + "`";
}

} // namespace framestepp
