#pragma once

#include "framestepp/ast.hpp"
#include "framestepp/diagnostic.hpp"

#include <cstddef>
#include <optional>

namespace framestepp {

inline constexpr std::size_t max_type_check_depth = 128;

using TypeCheckResult = std::optional<Diagnostic>;

/// Validates static semantics without modifying the parsed program.
class TypeChecker final {
  public:
    [[nodiscard]] TypeCheckResult check(const Program& program) const;
};

} // namespace framestepp
