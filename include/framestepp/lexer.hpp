#pragma once

#include "framestepp/diagnostic.hpp"
#include "framestepp/token.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace framestepp {

/// Maximum number of source tokens accepted by the default lexer configuration.
/// The mandatory end-of-file token is not counted toward this limit.
inline constexpr std::size_t max_source_tokens = 65'536;

using LexResult = std::variant<std::vector<Token>, Diagnostic>;

/// Converts borrowed UTF-8 source text into tokens with half-open byte spans.
class Lexer final {
  public:
    explicit Lexer(std::string_view source, std::size_t token_limit = max_source_tokens) noexcept;
    explicit Lexer(const SourceFile& source, std::size_t token_limit = max_source_tokens) noexcept;
    template <typename Text>
        requires(std::is_same_v<std::remove_cvref_t<Text>, std::string> &&
                 !std::is_lvalue_reference_v<Text>)
    Lexer(Text&&, std::size_t = max_source_tokens) = delete;
    Lexer(SourceFile&&, std::size_t = max_source_tokens) = delete;
    Lexer(const SourceFile&&, std::size_t = max_source_tokens) = delete;

    [[nodiscard]] LexResult lex() const;

  private:
    std::string_view source_;
    std::size_t token_limit_;
};

} // namespace framestepp
