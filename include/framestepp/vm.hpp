#pragma once

#include "framestepp/diagnostic.hpp"
#include "framestepp/value.hpp"

#include <cstddef>
#include <istream>
#include <string>
#include <variant>

namespace framestepp {

inline constexpr std::size_t max_execution_steps = 1'000'000;
inline constexpr std::size_t max_runtime_string_bytes = 1'048'576;
inline constexpr std::size_t max_buffered_output_bytes = 1'048'576;
inline constexpr std::size_t max_input_line_bytes = 65'536;
inline constexpr std::size_t max_total_input_bytes = 1'048'576;
inline constexpr std::size_t max_user_call_depth = 128;
inline constexpr std::size_t max_operand_stack_depth = 4'096;
inline constexpr std::size_t max_runtime_scope_depth = 256;

struct VmSuccess final {
    Value value;
    std::string output;
    std::size_t executed_instruction_count{0};
    std::size_t maximum_stack_depth{0};
    std::size_t maximum_user_call_depth{0};
};

struct VmFailure final {
    Diagnostic diagnostic;
    std::string output;
};

using VmResult = std::variant<VmSuccess, VmFailure>;

/// Verifies and executes one module using fresh state for every call.
class Vm final {
  public:
    [[nodiscard]] VmResult run(const BytecodeModule& module, std::istream& input) const;
};

} // namespace framestepp
