#include "framestepp/diagnostic.hpp"

#include "utf8.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace framestepp {
namespace {

[[nodiscard]] std::size_t code_point_count(const std::string_view text, const std::size_t start,
                                           const std::size_t end) noexcept {
    std::size_t count = 0;
    auto index = start;
    while (index < end) {
        const auto unit = detail::decode_utf8_unit(text, index);
        index += std::min(unit.width, end - index);
        ++count;
    }
    return count;
}

[[nodiscard]] std::string caret_padding(const std::string_view text, const std::size_t start,
                                        const std::size_t end) {
    std::string padding;
    padding.reserve(end - start);
    auto index = start;
    while (index < end) {
        padding.push_back(text[index] == '\t' ? '\t' : ' ');
        const auto unit = detail::decode_utf8_unit(text, index);
        index += std::min(unit.width, end - index);
    }
    return padding;
}

[[nodiscard]] std::string_view severity_name(const DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::error:
        return "error";
    case DiagnosticSeverity::warning:
        return "warning";
    }
    return "error";
}

} // namespace

std::string render_diagnostic(const SourceFile& source, const Diagnostic& diagnostic) {
    const auto text = source.text();
    const auto start = detail::utf8_boundary_before_or_at(text, diagnostic.span.start);
    const auto requested_end = std::max(diagnostic.span.start, diagnostic.span.end);
    const auto end = detail::utf8_boundary_after_or_at(text, requested_end);
    const auto location = source.location(start).value_or(SourceLocation{1, 1, 0});
    const auto source_line_span = source.line_span(location.line).value_or(Span{0, 0});
    const auto source_line = source.slice(source_line_span).value_or(std::string_view{});
    const auto highlight_start = std::clamp(start, source_line_span.start, source_line_span.end);
    const auto highlight_end =
        std::clamp(std::max(start, end), highlight_start, source_line_span.end);
    const auto highlight_width =
        std::max(std::size_t{1}, code_point_count(text, highlight_start, highlight_end));
    const auto gutter_width = std::to_string(location.line).size();

    std::ostringstream output;
    output << severity_name(diagnostic.severity) << ": " << diagnostic.message << '\n';
    output << std::string(gutter_width, ' ') << "--> " << source.path() << ':' << location.line
           << ':' << location.column << '\n';
    output << std::string(gutter_width, ' ') << " |\n";
    output << location.line << " | " << source_line << '\n';
    output << std::string(gutter_width, ' ') << " | "
           << caret_padding(text, source_line_span.start, highlight_start)
           << std::string(highlight_width, '^');
    return output.str();
}

} // namespace framestepp
