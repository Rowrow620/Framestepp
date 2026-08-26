#pragma once

#include "framestepp/source.hpp"
#include "framestepp/type.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace framestepp {

inline constexpr std::size_t max_bytecode_constants = 65'536;
inline constexpr std::size_t max_bytecode_functions = 16'384;
inline constexpr std::size_t max_bytecode_globals = 16'384;
inline constexpr std::size_t max_chunk_instructions = 65'536;
inline constexpr std::size_t max_module_instructions = 262'144;
inline constexpr std::size_t max_bytecode_stack_depth = 4'096;
inline constexpr std::size_t max_bytecode_scope_depth = 256;

struct ConstantId final {
    std::uint32_t value{0};

    friend constexpr bool operator==(const ConstantId&, const ConstantId&) noexcept = default;
};

struct FunctionId final {
    std::uint32_t value{0};

    friend constexpr bool operator==(const FunctionId&, const FunctionId&) noexcept = default;
};

struct GlobalId final {
    std::uint32_t value{0};

    friend constexpr bool operator==(const GlobalId&, const GlobalId&) noexcept = default;
};

struct InstructionId final {
    std::uint32_t value{0};

    friend constexpr bool operator==(const InstructionId&, const InstructionId&) noexcept = default;
};

using ConstantValue = std::variant<std::int64_t, bool, std::string>;

enum class UnaryCode {
    negate,
    not_,
};

enum class BinaryCode {
    add,
    subtract,
    multiply,
    divide,
    remainder,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

struct ConstantOp final {
    ConstantId constant;
    friend constexpr bool operator==(const ConstantOp&, const ConstantOp&) noexcept = default;
};

struct UnitOp final {
    friend constexpr bool operator==(const UnitOp&, const UnitOp&) noexcept = default;
};

struct LoadOp final {
    std::string name;
    friend bool operator==(const LoadOp&, const LoadOp&) = default;
};

struct InitializeGlobalOp final {
    GlobalId global;
    friend constexpr bool operator==(const InitializeGlobalOp&,
                                     const InitializeGlobalOp&) noexcept = default;
};

struct DefineLocalOp final {
    std::string name;
    bool mutable_binding{false};
    friend bool operator==(const DefineLocalOp&, const DefineLocalOp&) = default;
};

struct AssignOp final {
    std::string name;
    friend bool operator==(const AssignOp&, const AssignOp&) = default;
};

struct EnterScopeOp final {
    friend constexpr bool operator==(const EnterScopeOp&, const EnterScopeOp&) noexcept = default;
};

struct ExitScopeOp final {
    friend constexpr bool operator==(const ExitScopeOp&, const ExitScopeOp&) noexcept = default;
};

struct PopOp final {
    friend constexpr bool operator==(const PopOp&, const PopOp&) noexcept = default;
};

struct UnaryOp final {
    UnaryCode operation{UnaryCode::negate};
    friend constexpr bool operator==(const UnaryOp&, const UnaryOp&) noexcept = default;
};

struct BinaryOp final {
    BinaryCode operation{BinaryCode::add};
    friend constexpr bool operator==(const BinaryOp&, const BinaryOp&) noexcept = default;
};

struct JumpIfFalseOp final {
    InstructionId target;
    friend constexpr bool operator==(const JumpIfFalseOp&, const JumpIfFalseOp&) noexcept = default;
};

struct JumpOp final {
    InstructionId target;
    friend constexpr bool operator==(const JumpOp&, const JumpOp&) noexcept = default;
};

struct CallOp final {
    std::uint32_t argument_count{0};
    friend constexpr bool operator==(const CallOp&, const CallOp&) noexcept = default;
};

struct ReturnOp final {
    friend constexpr bool operator==(const ReturnOp&, const ReturnOp&) noexcept = default;
};

using Operation = std::variant<ConstantOp, UnitOp, LoadOp, InitializeGlobalOp, DefineLocalOp,
                               AssignOp, EnterScopeOp, ExitScopeOp, PopOp, UnaryOp, BinaryOp,
                               JumpIfFalseOp, JumpOp, CallOp, ReturnOp>;

struct Instruction final {
    Operation operation;
    Span span;

    friend bool operator==(const Instruction&, const Instruction&) = default;
};

struct Chunk final {
    std::vector<Instruction> instructions;

    friend bool operator==(const Chunk&, const Chunk&) = default;
};

struct GlobalBinding final {
    std::string name;
    bool mutable_binding{false};
    Span span;

    friend bool operator==(const GlobalBinding&, const GlobalBinding&) = default;
};

struct CompiledParameter final {
    std::string name;
    Type type;
    Span span;

    friend bool operator==(const CompiledParameter&, const CompiledParameter&) = default;
};

struct CompiledFunction final {
    std::string name;
    std::vector<CompiledParameter> parameters;
    Type result_type;
    Span span;
    Chunk chunk;

    friend bool operator==(const CompiledFunction&, const CompiledFunction&) = default;
};

struct BytecodeModule final {
    std::size_t source_size{0};
    std::vector<ConstantValue> constants;
    std::vector<GlobalBinding> globals;
    Chunk main;
    std::vector<CompiledFunction> functions;

    friend bool operator==(const BytecodeModule&, const BytecodeModule&) = default;
};

/// Produces deterministic, human-readable text for inspection and tests.
[[nodiscard]] std::string format_bytecode(const BytecodeModule& module);

} // namespace framestepp
