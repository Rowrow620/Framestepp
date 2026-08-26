#include "framestepp/bytecode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace framestepp {
namespace {

constexpr std::size_t operation_column_width = 24U;

template <typename Integer> [[nodiscard]] std::string decimal(const Integer value) {
    std::array<char, 32U> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) {
        return "?";
    }
    return std::string{buffer.data(), end};
}

template <typename Integer> [[nodiscard]] std::string padded_id(const Integer value) {
    auto result = decimal(value);
    if (result.size() < 4U) {
        result.insert(0U, 4U - result.size(), '0');
    }
    return result;
}

[[nodiscard]] std::string quoted(const std::string_view text) {
    constexpr std::string_view hex_digits = "0123456789ABCDEF";
    std::string result{"\""};
    result.reserve(text.size() + 2U);

    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20U || byte == 0x7FU) {
                result += "\\x";
                result.push_back(hex_digits[(byte >> 4U) & 0x0FU]);
                result.push_back(hex_digits[byte & 0x0FU]);
            } else {
                result.push_back(character);
            }
            break;
        }
    }

    result.push_back('"');
    return result;
}

[[nodiscard]] std::string displayed_name(const std::string_view name) {
    const bool needs_quotes =
        name.empty() || std::any_of(name.begin(), name.end(), [](const char ch) {
            const auto byte = static_cast<unsigned char>(ch);
            return byte <= 0x20U || byte == 0x7FU || ch == '"' || ch == '\\';
        });
    return needs_quotes ? quoted(name) : std::string{name};
}

void append_span(std::string& output, const Span span) {
    output.push_back('[');
    output += decimal(span.start);
    output += ", ";
    output += decimal(span.end);
    output.push_back(')');
}

[[nodiscard]] std::string unary_name(const UnaryCode operation) {
    switch (operation) {
    case UnaryCode::negate:
        return "NEGATE";
    case UnaryCode::not_:
        return "NOT";
    }
    return "UNKNOWN_UNARY";
}

[[nodiscard]] std::string binary_name(const BinaryCode operation) {
    switch (operation) {
    case BinaryCode::add:
        return "ADD";
    case BinaryCode::subtract:
        return "SUBTRACT";
    case BinaryCode::multiply:
        return "MULTIPLY";
    case BinaryCode::divide:
        return "DIVIDE";
    case BinaryCode::remainder:
        return "REMAINDER";
    case BinaryCode::equal:
        return "EQUAL";
    case BinaryCode::not_equal:
        return "NOT_EQUAL";
    case BinaryCode::less:
        return "LESS";
    case BinaryCode::less_equal:
        return "LESS_EQUAL";
    case BinaryCode::greater:
        return "GREATER";
    case BinaryCode::greater_equal:
        return "GREATER_EQUAL";
    }
    return "UNKNOWN_BINARY";
}

[[nodiscard]] std::string format_operation(const Operation& operation) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, ConstantOp>) {
                return "CONSTANT " + padded_id(value.constant.value);
            } else if constexpr (std::is_same_v<Value, UnitOp>) {
                return "UNIT";
            } else if constexpr (std::is_same_v<Value, LoadOp>) {
                return "LOAD " + displayed_name(value.name);
            } else if constexpr (std::is_same_v<Value, InitializeGlobalOp>) {
                return "INITIALIZE_GLOBAL " + padded_id(value.global.value);
            } else if constexpr (std::is_same_v<Value, DefineLocalOp>) {
                return "DEFINE_LOCAL " + std::string{value.mutable_binding ? "mut " : "let "} +
                       displayed_name(value.name);
            } else if constexpr (std::is_same_v<Value, AssignOp>) {
                return "ASSIGN " + displayed_name(value.name);
            } else if constexpr (std::is_same_v<Value, EnterScopeOp>) {
                return "ENTER_SCOPE";
            } else if constexpr (std::is_same_v<Value, ExitScopeOp>) {
                return "EXIT_SCOPE";
            } else if constexpr (std::is_same_v<Value, PopOp>) {
                return "POP";
            } else if constexpr (std::is_same_v<Value, UnaryOp>) {
                return unary_name(value.operation);
            } else if constexpr (std::is_same_v<Value, BinaryOp>) {
                return binary_name(value.operation);
            } else if constexpr (std::is_same_v<Value, JumpIfFalseOp>) {
                return "JUMP_IF_FALSE " + padded_id(value.target.value);
            } else if constexpr (std::is_same_v<Value, JumpOp>) {
                return "JUMP " + padded_id(value.target.value);
            } else if constexpr (std::is_same_v<Value, CallOp>) {
                return "CALL " + decimal(value.argument_count);
            } else {
                static_assert(std::is_same_v<Value, ReturnOp>);
                return "RETURN";
            }
        },
        operation);
}

[[nodiscard]] std::string format_constant(const ConstantValue& constant) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, std::int64_t>) {
                return "Integer " + decimal(value);
            } else if constexpr (std::is_same_v<Value, bool>) {
                return std::string{"Boolean "} + (value ? "true" : "false");
            } else {
                static_assert(std::is_same_v<Value, std::string>);
                return "String " + quoted(value);
            }
        },
        constant);
}

void append_chunk(std::string& output, const Chunk& chunk) {
    for (std::size_t index = 0; index < chunk.instructions.size(); ++index) {
        const auto& instruction = chunk.instructions[index];
        const auto operation = format_operation(instruction.operation);
        output += padded_id(index);
        output.push_back(' ');
        output += operation;
        if (operation.size() < operation_column_width) {
            output.append(operation_column_width - operation.size(), ' ');
        }
        output += " @ ";
        append_span(output, instruction.span);
        output.push_back('\n');
    }
}

void append_function_header(std::string& output, const std::size_t index,
                            const CompiledFunction& function) {
    output += "== function ";
    output += padded_id(index);
    output.push_back(' ');
    output += displayed_name(function.name);
    output.push_back('(');
    for (std::size_t parameter_index = 0; parameter_index < function.parameters.size();
         ++parameter_index) {
        if (parameter_index != 0U) {
            output += ", ";
        }
        const auto& parameter = function.parameters[parameter_index];
        output += displayed_name(parameter.name);
        output += ": ";
        output += format_type(parameter.type);
    }
    output += ") -> ";
    output += format_type(function.result_type);
    output += " @ ";
    append_span(output, function.span);
    output += " ==\n";
}

} // namespace

std::string format_bytecode(const BytecodeModule& module) {
    std::string output;
    output += "== constants ==\n";
    for (std::size_t index = 0; index < module.constants.size(); ++index) {
        output += padded_id(index);
        output.push_back(' ');
        output += format_constant(module.constants[index]);
        output.push_back('\n');
    }

    output += "== globals ==\n";
    for (std::size_t index = 0; index < module.globals.size(); ++index) {
        const auto& global = module.globals[index];
        output += padded_id(index);
        output += global.mutable_binding ? " mut " : " let ";
        output += displayed_name(global.name);
        output += " @ ";
        append_span(output, global.span);
        output.push_back('\n');
    }

    output += "== main ==\n";
    append_chunk(output, module.main);

    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        append_function_header(output, index, module.functions[index]);
        append_chunk(output, module.functions[index].chunk);
    }
    return output;
}

} // namespace framestepp
