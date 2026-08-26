#pragma once

#include "framestepp/ast.hpp"
#include "framestepp/bytecode.hpp"
#include "framestepp/diagnostic.hpp"

#include <variant>

namespace framestepp {

using CompileResult = std::variant<BytecodeModule, Diagnostic>;

/// Type-checks and lowers a parser-produced program into deterministic bytecode.
/// Recursive AST ownership edges must be non-null, as guaranteed by Parser.
class Compiler final {
  public:
    [[nodiscard]] CompileResult compile(const Program& program) const;
};

} // namespace framestepp
