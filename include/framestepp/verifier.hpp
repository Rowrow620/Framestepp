#pragma once

#include "framestepp/bytecode.hpp"
#include "framestepp/diagnostic.hpp"

#include <cstddef>
#include <variant>

namespace framestepp {

struct VerificationSummary final {
    std::size_t instruction_count{0};
    std::size_t maximum_stack_depth{0};
    std::size_t maximum_scope_depth{0};

    friend constexpr bool operator==(const VerificationSummary&,
                                     const VerificationSummary&) noexcept = default;
};

using VerificationResult = std::variant<VerificationSummary, Diagnostic>;

/// Rejects malformed bytecode before it can reach the virtual machine.
class BytecodeVerifier final {
  public:
    [[nodiscard]] VerificationResult verify(const BytecodeModule& module) const;
};

} // namespace framestepp
