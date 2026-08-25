#pragma once

#include "framestepp/source.hpp"

#include <string>

namespace framestepp {

enum class DiagnosticSeverity {
    error,
    warning,
};

/// An expected source-language failure that can be returned by value.
struct Diagnostic final {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string message;
    Span span;
};

/// Renders a diagnostic with its file position, source line, and caret marker.
[[nodiscard]] std::string render_diagnostic(const SourceFile& source, const Diagnostic& diagnostic);

} // namespace framestepp
