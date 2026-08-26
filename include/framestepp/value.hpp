#pragma once

#include "framestepp/bytecode.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace framestepp {

struct UnitValue final {
    friend constexpr bool operator==(const UnitValue&, const UnitValue&) noexcept = default;
};

enum class BuiltinCode {
    frameout,
    print,
    framein,
};

struct FunctionValue final {
    FunctionId function;

    friend constexpr bool operator==(const FunctionValue&, const FunctionValue&) noexcept = default;
};

struct BuiltinValue final {
    BuiltinCode builtin{BuiltinCode::frameout};

    friend constexpr bool operator==(const BuiltinValue&, const BuiltinValue&) noexcept = default;
};

using Value = std::variant<std::int64_t, bool, std::string, UnitValue, FunctionValue, BuiltinValue>;

/// Produces the stable source-facing representation used by output built-ins.
[[nodiscard]] std::string format_value(const Value& value, const BytecodeModule& module);

} // namespace framestepp
