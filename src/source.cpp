#include "framestepp/source.hpp"

#include "utf8.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

namespace framestepp {
namespace {

[[nodiscard]] std::optional<std::size_t> utf8_column(const std::string_view text,
                                                     const std::size_t line_start,
                                                     const std::size_t byte_offset) noexcept {
    std::size_t column = 1;
    auto index = line_start;
    while (index < byte_offset) {
        const auto unit = detail::decode_utf8_unit(text, index);
        if (index + unit.width > byte_offset) {
            return std::nullopt;
        }
        index += unit.width;
        ++column;
    }
    return column;
}

} // namespace

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)), line_starts_{0} {
    for (std::size_t index = 0; index < text_.size(); ++index) {
        if (text_[index] == '\r') {
            if (index + 1 < text_.size() && text_[index + 1] == '\n') {
                ++index;
            }
            line_starts_.push_back(index + 1);
        } else if (text_[index] == '\n') {
            line_starts_.push_back(index + 1);
        }
    }
}

std::string_view SourceFile::path() const noexcept { return path_; }

std::string_view SourceFile::text() const noexcept { return text_; }

std::size_t SourceFile::size() const noexcept { return text_.size(); }

bool SourceFile::contains(const Span span) const noexcept {
    return span.is_valid() && span.end <= text_.size();
}

std::optional<std::string_view> SourceFile::slice(const Span span) const noexcept {
    if (!contains(span)) {
        return std::nullopt;
    }
    return std::string_view{text_}.substr(span.start, span.size());
}

std::optional<SourceLocation> SourceFile::location(const std::size_t byte_offset) const noexcept {
    if (byte_offset > text_.size()) {
        return std::nullopt;
    }
    const auto next_line = std::upper_bound(line_starts_.begin(), line_starts_.end(), byte_offset);
    const auto line_index =
        static_cast<std::size_t>(std::distance(line_starts_.begin(), next_line) - 1);
    const auto line_start = line_starts_[line_index];

    auto column_offset = byte_offset;
    if (byte_offset > line_start && byte_offset < text_.size() && text_[byte_offset] == '\n' &&
        text_[byte_offset - 1] == '\r') {
        --column_offset;
    }
    const auto column = utf8_column(text_, line_start, column_offset);
    if (!column.has_value()) {
        return std::nullopt;
    }

    return SourceLocation{
        line_index + 1,
        *column,
        byte_offset,
    };
}

std::optional<Span> SourceFile::line_span(const std::size_t line_number) const noexcept {
    if (line_number == 0 || line_number > line_starts_.size()) {
        return std::nullopt;
    }

    const auto line_index = line_number - 1;
    const auto start = line_starts_[line_index];
    auto end = line_index + 1 < line_starts_.size() ? line_starts_[line_index + 1] : text_.size();
    if (end > start && text_[end - 1] == '\n') {
        --end;
    }
    if (end > start && text_[end - 1] == '\r') {
        --end;
    }
    return Span{start, end};
}

std::optional<std::string_view>
SourceFile::line_text(const std::size_t line_number) const noexcept {
    const auto span = line_span(line_number);
    if (!span.has_value()) {
        return std::nullopt;
    }
    return slice(*span);
}

} // namespace framestepp
