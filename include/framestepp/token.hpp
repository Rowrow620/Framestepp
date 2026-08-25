#pragma once

#include "framestepp/source.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace framestepp {

enum class TokenKind {
    identifier,
    integer,
    string,
    fn_keyword,
    let_keyword,
    mut_keyword,
    if_keyword,
    else_keyword,
    match_keyword,
    return_keyword,
    true_keyword,
    false_keyword,
    left_parenthesis,
    right_parenthesis,
    left_brace,
    right_brace,
    left_bracket,
    right_bracket,
    comma,
    colon,
    semicolon,
    dot,
    plus,
    minus,
    star,
    slash,
    percent,
    equal,
    equal_equal,
    bang,
    bang_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    arrow,
    pipe,
    end_of_file,
};

/// Identifiers and decoded strings use std::string; integers use std::int64_t.
using TokenValue = std::variant<std::monostate, std::int64_t, std::string>;

struct Token final {
    TokenKind kind{TokenKind::end_of_file};
    Span span;
    TokenValue value;

    friend bool operator==(const Token&, const Token&) = default;
};

/// Returns a stable, human-readable name or spelling for a token kind.
[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;

/// Returns a stable description suitable for token listings and diagnostics.
[[nodiscard]] std::string token_description(const Token& token);

} // namespace framestepp
