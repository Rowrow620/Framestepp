#include "framestepp/value.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

namespace framestepp {
namespace {

[[nodiscard]] std::string format_integer(const std::int64_t value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string{buffer.data(), result.ptr};
}

[[nodiscard]] std::string format_builtin(const BuiltinCode builtin) {
    switch (builtin) {
    case BuiltinCode::frameout:
        return "<builtin frameout>";
    case BuiltinCode::print:
        return "<builtin print>";
    case BuiltinCode::framein:
        return "<builtin framein>";
    }
    return "<builtin ?>";
}

} // namespace

std::string format_value(const Value& value, const BytecodeModule& module) {
    if (value.valueless_by_exception()) {
        return "<invalid value>";
    }

    return std::visit(
        [&module](const auto& stored) -> std::string {
            using Stored = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::is_same_v<Stored, std::int64_t>) {
                return format_integer(stored);
            } else if constexpr (std::is_same_v<Stored, bool>) {
                return stored ? "true" : "false";
            } else if constexpr (std::is_same_v<Stored, std::string>) {
                return stored;
            } else if constexpr (std::is_same_v<Stored, UnitValue>) {
                return "()";
            } else if constexpr (std::is_same_v<Stored, FunctionValue>) {
                const auto index = static_cast<std::size_t>(stored.function.value);
                if (index >= module.functions.size()) {
                    return "<fn ?>";
                }
                return "<fn " + module.functions[index].name + ">";
            } else {
                return format_builtin(stored.builtin);
            }
        },
        value);
}

} // namespace framestepp
