#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace framestepp {

/// A half-open byte range into a SourceFile.
struct Span final {
    std::size_t start{0};
    std::size_t end{0};

    [[nodiscard]] constexpr bool is_valid() const noexcept { return start <= end; }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return is_valid() ? end - start : 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return start == end; }

    [[nodiscard]] constexpr bool contains(const std::size_t byte_offset) const noexcept {
        return start <= byte_offset && byte_offset < end;
    }

    friend constexpr bool operator==(const Span&, const Span&) noexcept = default;
};

/// A one-based human-readable position plus its zero-based byte offset.
struct SourceLocation final {
    std::size_t line{1};
    std::size_t column{1};
    std::size_t byte_offset{0};

    friend constexpr bool operator==(const SourceLocation&,
                                     const SourceLocation&) noexcept = default;
};

/// Owns a source path and its UTF-8 encoded contents.
class SourceFile final {
  public:
    SourceFile(std::string path, std::string text);

    [[nodiscard]] std::string_view path() const noexcept;
    [[nodiscard]] std::string_view text() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool contains(Span span) const noexcept;
    [[nodiscard]] std::optional<std::string_view> slice(Span span) const noexcept;
    [[nodiscard]] std::optional<SourceLocation> location(std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::optional<Span> line_span(std::size_t line_number) const noexcept;
    [[nodiscard]] std::optional<std::string_view> line_text(std::size_t line_number) const noexcept;

  private:
    std::string path_;
    std::string text_;
    std::vector<std::size_t> line_starts_;
};

} // namespace framestepp
