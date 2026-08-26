#include "framestepp/vm.hpp"

#include "framestepp/verifier.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ios>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

struct RuntimeBinding final {
    Value value;
    bool mutable_binding{false};
};

using Scope = std::unordered_map<std::string, RuntimeBinding>;

struct CallFrame final {
    std::optional<FunctionId> function;
    std::size_t instruction_pointer{0};
    std::size_t stack_base{0};
    std::vector<Scope> scopes;
    std::size_t scope_floor{0};
    Span fallback_span;
};

struct AssignmentTarget final {
    RuntimeBinding* binding{nullptr};
    bool found{false};
};

[[nodiscard]] std::string_view value_type_name(const Value& value) noexcept {
    if (std::holds_alternative<std::int64_t>(value)) {
        return "Int";
    }
    if (std::holds_alternative<bool>(value)) {
        return "Bool";
    }
    if (std::holds_alternative<std::string>(value)) {
        return "String";
    }
    if (std::holds_alternative<UnitValue>(value)) {
        return "Unit";
    }
    return "Function";
}

[[nodiscard]] std::string_view unary_spelling(const UnaryCode operation) noexcept {
    switch (operation) {
    case UnaryCode::negate:
        return "-";
    case UnaryCode::not_:
        return "!";
    }
    return "?";
}

[[nodiscard]] std::string_view binary_spelling(const BinaryCode operation) noexcept {
    switch (operation) {
    case BinaryCode::add:
        return "+";
    case BinaryCode::subtract:
        return "-";
    case BinaryCode::multiply:
        return "*";
    case BinaryCode::divide:
        return "/";
    case BinaryCode::remainder:
        return "%";
    case BinaryCode::equal:
        return "==";
    case BinaryCode::not_equal:
        return "!=";
    case BinaryCode::less:
        return "<";
    case BinaryCode::less_equal:
        return "<=";
    case BinaryCode::greater:
        return ">";
    case BinaryCode::greater_equal:
        return ">=";
    }
    return "?";
}

[[nodiscard]] bool checked_add(const std::int64_t left, const std::int64_t right,
                               std::int64_t& result) noexcept {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right)) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_subtract(const std::int64_t left, const std::int64_t right,
                                    std::int64_t& result) noexcept {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left < minimum + right) || (right < 0 && left > maximum + right)) {
        return false;
    }
    result = left - right;
    return true;
}

[[nodiscard]] bool checked_multiply(const std::int64_t left, const std::int64_t right,
                                    std::int64_t& result) noexcept {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();

    if (left > 0) {
        if ((right > 0 && left > maximum / right) || (right < 0 && right < minimum / left)) {
            return false;
        }
    } else if (left < 0) {
        if ((right > 0 && left < minimum / right) || (right < 0 && left < maximum / right)) {
            return false;
        }
    }

    result = left * right;
    return true;
}

class Execution final {
  public:
    Execution(const BytecodeModule& module, std::istream& input)
        : module_{module}, input_{input}, globals_(module.globals.size()) {
        global_ids_.reserve(module.globals.size());
        for (std::size_t index = 0; index < module.globals.size(); ++index) {
            global_ids_.emplace(module.globals[index].name, index);
        }

        function_ids_.reserve(module.functions.size());
        for (std::size_t index = 0; index < module.functions.size(); ++index) {
            function_ids_.emplace(module.functions[index].name,
                                  FunctionId{static_cast<std::uint32_t>(index)});
        }

        frames_.push_back(CallFrame{std::nullopt, 0U, 0U, {}, 0U, Span{}});
    }

    [[nodiscard]] VmResult run() {
        while (!diagnostic_ && !frames_.empty()) {
            execute_next();
        }

        if (diagnostic_) {
            return VmFailure{std::move(*diagnostic_), std::move(output_)};
        }
        if (!final_value_) {
            fail_malformed("execution ended without a return value", Span{});
            return VmFailure{std::move(*diagnostic_), std::move(output_)};
        }
        return VmSuccess{std::move(*final_value_), std::move(output_), executed_instruction_count_,
                         maximum_stack_depth_, maximum_user_call_depth_};
    }

  private:
    void fail(std::string message, const Span span) {
        if (!diagnostic_) {
            diagnostic_ = Diagnostic{DiagnosticSeverity::error, std::move(message), span};
        }
    }

    void fail_malformed(std::string message, const Span span) {
        fail("malformed bytecode: " + std::move(message), span);
    }

    [[nodiscard]] const Chunk* current_chunk(const Span span) {
        if (frames_.empty()) {
            fail_malformed("missing call frame", span);
            return nullptr;
        }
        const auto function = frames_.back().function;
        if (!function) {
            return &module_.main;
        }
        const auto index = static_cast<std::size_t>(function->value);
        if (index >= module_.functions.size()) {
            fail_malformed("function index is out of range", span);
            return nullptr;
        }
        return &module_.functions[index].chunk;
    }

    void execute_next() {
        const Span fallback_span = frames_.back().fallback_span;
        const Chunk* chunk = current_chunk(fallback_span);
        if (chunk == nullptr) {
            return;
        }

        const std::size_t offset = frames_.back().instruction_pointer;
        if (offset >= chunk->instructions.size()) {
            fail_malformed("instruction pointer is out of range", fallback_span);
            return;
        }
        const Instruction& instruction = chunk->instructions[offset];
        if (executed_instruction_count_ >= max_execution_steps) {
            fail("maximum execution step count of " + std::to_string(max_execution_steps) +
                     " exceeded",
                 instruction.span);
            return;
        }
        if (instruction.operation.valueless_by_exception()) {
            fail_malformed("instruction contains a valueless operation", instruction.span);
            return;
        }

        ++executed_instruction_count_;
        frames_.back().instruction_pointer = offset + 1U;
        execute(instruction);
    }

    void execute(const Instruction& instruction) {
        const auto& operation = instruction.operation;
        if (const auto* constant = std::get_if<ConstantOp>(&operation)) {
            execute_constant(*constant, instruction.span);
        } else if (std::holds_alternative<UnitOp>(operation)) {
            push(Value{UnitValue{}}, instruction.span);
        } else if (const auto* load = std::get_if<LoadOp>(&operation)) {
            execute_load(*load, instruction.span);
        } else if (const auto* initialize = std::get_if<InitializeGlobalOp>(&operation)) {
            execute_initialize_global(*initialize, instruction.span);
        } else if (const auto* define = std::get_if<DefineLocalOp>(&operation)) {
            execute_define_local(*define, instruction.span);
        } else if (const auto* assign = std::get_if<AssignOp>(&operation)) {
            execute_assign(*assign, instruction.span);
        } else if (std::holds_alternative<EnterScopeOp>(operation)) {
            execute_enter_scope(instruction.span);
        } else if (std::holds_alternative<ExitScopeOp>(operation)) {
            execute_exit_scope(instruction.span);
        } else if (std::holds_alternative<PopOp>(operation)) {
            if (require_stack(1U, "POP", instruction.span)) {
                stack_.pop_back();
            }
        } else if (const auto* unary = std::get_if<UnaryOp>(&operation)) {
            execute_unary(*unary, instruction.span);
        } else if (const auto* binary = std::get_if<BinaryOp>(&operation)) {
            execute_binary(*binary, instruction.span);
        } else if (const auto* conditional = std::get_if<JumpIfFalseOp>(&operation)) {
            execute_jump_if_false(*conditional, instruction.span);
        } else if (const auto* jump = std::get_if<JumpOp>(&operation)) {
            execute_jump(*jump, instruction.span);
        } else if (const auto* call = std::get_if<CallOp>(&operation)) {
            execute_call(*call, instruction.span);
        } else if (std::holds_alternative<ReturnOp>(operation)) {
            execute_return(instruction.span);
        } else {
            fail_malformed("unknown operation", instruction.span);
        }
    }

    [[nodiscard]] bool require_stack(const std::size_t count, const std::string_view operation,
                                     const Span span) {
        if (frames_.empty()) {
            fail_malformed("missing call frame while executing " + std::string{operation}, span);
            return false;
        }
        const std::size_t base = frames_.back().stack_base;
        if (base > stack_.size() || stack_.size() - base < count) {
            fail_malformed("operand stack underflow while executing " + std::string{operation},
                           span);
            return false;
        }
        return true;
    }

    bool push(Value value, const Span span) {
        if (stack_.size() >= max_operand_stack_depth) {
            fail("maximum operand stack depth of " + std::to_string(max_operand_stack_depth) +
                     " exceeded",
                 span);
            return false;
        }
        stack_.push_back(std::move(value));
        maximum_stack_depth_ = std::max(maximum_stack_depth_, stack_.size());
        return true;
    }

    void execute_constant(const ConstantOp& operation, const Span span) {
        const auto index = static_cast<std::size_t>(operation.constant.value);
        if (index >= module_.constants.size()) {
            fail_malformed("constant index is out of range", span);
            return;
        }
        const ConstantValue& constant = module_.constants[index];
        if (constant.valueless_by_exception()) {
            fail_malformed("constant table contains a valueless entry", span);
            return;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&constant)) {
            push(Value{*integer}, span);
        } else if (const auto* boolean = std::get_if<bool>(&constant)) {
            push(Value{*boolean}, span);
        } else if (const auto* text = std::get_if<std::string>(&constant)) {
            if (text->size() > max_runtime_string_bytes) {
                fail_maximum_string(span);
            } else if (!detail::is_valid_utf8(*text)) {
                fail_malformed("string constant is not valid UTF-8", span);
            } else {
                push(Value{*text}, span);
            }
        } else {
            fail_malformed("constant has an unknown value type", span);
        }
    }

    [[nodiscard]] std::optional<Value> lookup(const std::string& name) const {
        if (!frames_.empty()) {
            const auto& scopes = frames_.back().scopes;
            for (auto scope = scopes.crbegin(); scope != scopes.crend(); ++scope) {
                const auto binding = scope->find(name);
                if (binding != scope->end()) {
                    return binding->second.value;
                }
            }
        }

        const auto global_id = global_ids_.find(name);
        if (global_id != global_ids_.end() && global_id->second < globals_.size() &&
            globals_[global_id->second]) {
            return globals_[global_id->second]->value;
        }

        const auto function = function_ids_.find(name);
        if (function != function_ids_.end()) {
            return Value{FunctionValue{function->second}};
        }
        if (name == "frameout") {
            return Value{BuiltinValue{BuiltinCode::frameout}};
        }
        if (name == "print") {
            return Value{BuiltinValue{BuiltinCode::print}};
        }
        if (name == "framein") {
            return Value{BuiltinValue{BuiltinCode::framein}};
        }
        return std::nullopt;
    }

    void execute_load(const LoadOp& operation, const Span span) {
        auto value = lookup(operation.name);
        if (!value) {
            fail_malformed("unknown binding `" + operation.name + "`", span);
            return;
        }
        push(std::move(*value), span);
    }

    void execute_initialize_global(const InitializeGlobalOp& operation, const Span span) {
        if (!require_stack(1U, "INITIALIZE_GLOBAL", span)) {
            return;
        }
        if (frames_.size() != 1U || frames_.back().function || !frames_.back().scopes.empty()) {
            fail_malformed("global initialization requires the main root scope", span);
            return;
        }
        const auto index = static_cast<std::size_t>(operation.global.value);
        if (index >= globals_.size() || index >= module_.globals.size()) {
            fail_malformed("global index is out of range", span);
            return;
        }
        if (index != initialized_globals_ || globals_[index]) {
            fail_malformed("globals must be initialized exactly once in source order", span);
            return;
        }

        Value value = std::move(stack_.back());
        stack_.pop_back();
        globals_[index] = RuntimeBinding{std::move(value), module_.globals[index].mutable_binding};
        ++initialized_globals_;
    }

    void execute_define_local(const DefineLocalOp& operation, const Span span) {
        if (!require_stack(1U, "DEFINE_LOCAL", span) || frames_.empty()) {
            return;
        }
        auto& frame = frames_.back();
        if (frame.scopes.size() <= frame.scope_floor) {
            fail_malformed("local definition requires an entered lexical scope", span);
            return;
        }
        Scope& scope = frame.scopes.back();
        if (scope.contains(operation.name)) {
            fail_malformed("local `" + operation.name + "` is already defined", span);
            return;
        }

        Value value = std::move(stack_.back());
        stack_.pop_back();
        scope.emplace(operation.name, RuntimeBinding{std::move(value), operation.mutable_binding});
    }

    [[nodiscard]] AssignmentTarget assignment_target(const std::string& name) {
        if (!frames_.empty()) {
            auto& scopes = frames_.back().scopes;
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                const auto binding = scope->find(name);
                if (binding != scope->end()) {
                    return AssignmentTarget{&binding->second, true};
                }
            }
        }

        const auto global_id = global_ids_.find(name);
        if (global_id != global_ids_.end() && global_id->second < globals_.size() &&
            globals_[global_id->second]) {
            return AssignmentTarget{&*globals_[global_id->second], true};
        }
        if (function_ids_.contains(name) || name == "frameout" || name == "print" ||
            name == "framein") {
            return AssignmentTarget{nullptr, true};
        }
        return AssignmentTarget{};
    }

    void execute_assign(const AssignOp& operation, const Span span) {
        if (!require_stack(1U, "ASSIGN", span)) {
            return;
        }
        const auto target = assignment_target(operation.name);
        if (!target.found) {
            fail_malformed("unknown binding `" + operation.name + "`", span);
        } else if (target.binding == nullptr || !target.binding->mutable_binding) {
            fail_malformed("cannot assign to immutable binding `" + operation.name + "`", span);
        } else {
            target.binding->value = stack_.back();
        }
    }

    void execute_enter_scope(const Span span) {
        if (frames_.empty()) {
            fail_malformed("cannot enter a scope without a call frame", span);
            return;
        }
        auto& scopes = frames_.back().scopes;
        if (scopes.size() >= max_runtime_scope_depth) {
            fail("maximum lexical scope depth of " + std::to_string(max_runtime_scope_depth) +
                     " exceeded",
                 span);
            return;
        }
        scopes.emplace_back();
    }

    void execute_exit_scope(const Span span) {
        if (frames_.empty() || frames_.back().scopes.size() <= frames_.back().scope_floor) {
            fail_malformed("cannot exit a call frame's base scope", span);
            return;
        }
        frames_.back().scopes.pop_back();
    }

    void execute_unary(const UnaryOp& operation, const Span span) {
        if (!require_stack(1U, "UNARY", span)) {
            return;
        }
        Value value = std::move(stack_.back());
        stack_.pop_back();

        if (operation.operation == UnaryCode::negate) {
            const auto* integer = std::get_if<std::int64_t>(&value);
            if (integer == nullptr) {
                fail("unary `-` requires Int, found " + std::string{value_type_name(value)}, span);
            } else if (*integer == std::numeric_limits<std::int64_t>::min()) {
                fail("integer overflow while evaluating unary `-`", span);
            } else {
                push(Value{-*integer}, span);
            }
            return;
        }
        if (operation.operation == UnaryCode::not_) {
            const auto* boolean = std::get_if<bool>(&value);
            if (boolean == nullptr) {
                fail("unary `!` requires Bool, found " + std::string{value_type_name(value)}, span);
            } else {
                push(Value{!*boolean}, span);
            }
            return;
        }
        fail_malformed("invalid unary operation `" +
                           std::string{unary_spelling(operation.operation)} + "`",
                       span);
    }

    void execute_binary(const BinaryOp& operation, const Span span) {
        if (!require_stack(2U, "BINARY", span)) {
            return;
        }
        Value right = std::move(stack_.back());
        stack_.pop_back();
        Value left = std::move(stack_.back());
        stack_.pop_back();

        switch (operation.operation) {
        case BinaryCode::add:
            execute_add(std::move(left), std::move(right), span);
            return;
        case BinaryCode::subtract:
        case BinaryCode::multiply:
        case BinaryCode::divide:
        case BinaryCode::remainder:
            execute_integer_binary(operation.operation, std::move(left), std::move(right), span);
            return;
        case BinaryCode::equal:
        case BinaryCode::not_equal:
            execute_equality(operation.operation, left, right, span);
            return;
        case BinaryCode::less:
        case BinaryCode::less_equal:
        case BinaryCode::greater:
        case BinaryCode::greater_equal:
            execute_comparison(operation.operation, left, right, span);
            return;
        }
        fail_malformed("invalid binary operation", span);
    }

    void execute_add(Value left, Value right, const Span span) {
        if (const auto* left_integer = std::get_if<std::int64_t>(&left)) {
            const auto* right_integer = std::get_if<std::int64_t>(&right);
            if (right_integer == nullptr) {
                fail_binary_type(BinaryCode::add, left, right, span);
                return;
            }
            std::int64_t result = 0;
            if (!checked_add(*left_integer, *right_integer, result)) {
                fail_integer_overflow(BinaryCode::add, span);
            } else {
                push(Value{result}, span);
            }
            return;
        }

        auto* left_text = std::get_if<std::string>(&left);
        const auto* right_text = std::get_if<std::string>(&right);
        if (left_text == nullptr || right_text == nullptr) {
            fail_binary_type(BinaryCode::add, left, right, span);
            return;
        }
        if (left_text->size() > max_runtime_string_bytes ||
            right_text->size() > max_runtime_string_bytes - left_text->size()) {
            fail_maximum_string(span);
            return;
        }
        try {
            left_text->append(*right_text);
        } catch (const std::bad_alloc&) {
            fail("unable to reserve string memory", span);
            return;
        }
        push(std::move(left), span);
    }

    void execute_integer_binary(const BinaryCode operation, const Value left, const Value right,
                                const Span span) {
        const auto* left_integer = std::get_if<std::int64_t>(&left);
        const auto* right_integer = std::get_if<std::int64_t>(&right);
        if (left_integer == nullptr || right_integer == nullptr) {
            fail_binary_type(operation, left, right, span);
            return;
        }

        std::int64_t result = 0;
        bool valid = false;
        switch (operation) {
        case BinaryCode::subtract:
            valid = checked_subtract(*left_integer, *right_integer, result);
            break;
        case BinaryCode::multiply:
            valid = checked_multiply(*left_integer, *right_integer, result);
            break;
        case BinaryCode::divide:
            if (*right_integer == 0) {
                fail("division by zero", span);
                return;
            }
            if (*left_integer == std::numeric_limits<std::int64_t>::min() && *right_integer == -1) {
                fail_integer_overflow(operation, span);
                return;
            }
            result = *left_integer / *right_integer;
            valid = true;
            break;
        case BinaryCode::remainder:
            if (*right_integer == 0) {
                fail("remainder by zero", span);
                return;
            }
            if (*left_integer == std::numeric_limits<std::int64_t>::min() && *right_integer == -1) {
                fail_integer_overflow(operation, span);
                return;
            }
            result = *left_integer % *right_integer;
            valid = true;
            break;
        default:
            fail_malformed("non-arithmetic operation reached integer arithmetic", span);
            return;
        }

        if (!valid) {
            fail_integer_overflow(operation, span);
        } else {
            push(Value{result}, span);
        }
    }

    void execute_equality(const BinaryCode operation, const Value& left, const Value& right,
                          const Span span) {
        std::optional<bool> equal;
        if (const auto* left_integer = std::get_if<std::int64_t>(&left)) {
            if (const auto* right_integer = std::get_if<std::int64_t>(&right)) {
                equal = *left_integer == *right_integer;
            }
        } else if (const auto* left_boolean = std::get_if<bool>(&left)) {
            if (const auto* right_boolean = std::get_if<bool>(&right)) {
                equal = *left_boolean == *right_boolean;
            }
        } else if (const auto* left_text = std::get_if<std::string>(&left)) {
            if (const auto* right_text = std::get_if<std::string>(&right)) {
                equal = *left_text == *right_text;
            }
        } else if (std::holds_alternative<UnitValue>(left) &&
                   std::holds_alternative<UnitValue>(right)) {
            equal = true;
        } else if (std::holds_alternative<FunctionValue>(left) ||
                   std::holds_alternative<BuiltinValue>(left) ||
                   std::holds_alternative<FunctionValue>(right) ||
                   std::holds_alternative<BuiltinValue>(right)) {
            fail("function values cannot be compared", span);
            return;
        }

        if (!equal) {
            fail_binary_type(operation, left, right, span);
            return;
        }
        push(Value{operation == BinaryCode::equal ? *equal : !*equal}, span);
    }

    void execute_comparison(const BinaryCode operation, const Value& left, const Value& right,
                            const Span span) {
        const auto* left_integer = std::get_if<std::int64_t>(&left);
        const auto* right_integer = std::get_if<std::int64_t>(&right);
        if (left_integer == nullptr || right_integer == nullptr) {
            fail_binary_type(operation, left, right, span);
            return;
        }

        bool result = false;
        switch (operation) {
        case BinaryCode::less:
            result = *left_integer < *right_integer;
            break;
        case BinaryCode::less_equal:
            result = *left_integer <= *right_integer;
            break;
        case BinaryCode::greater:
            result = *left_integer > *right_integer;
            break;
        case BinaryCode::greater_equal:
            result = *left_integer >= *right_integer;
            break;
        default:
            fail_malformed("non-comparison operation reached comparison", span);
            return;
        }
        push(Value{result}, span);
    }

    void fail_binary_type(const BinaryCode operation, const Value& left, const Value& right,
                          const Span span) {
        fail("operator `" + std::string{binary_spelling(operation)} + "` is not defined for " +
                 std::string{value_type_name(left)} + " and " + std::string{value_type_name(right)},
             span);
    }

    void fail_integer_overflow(const BinaryCode operation, const Span span) {
        fail("integer overflow while evaluating `" + std::string{binary_spelling(operation)} + "`",
             span);
    }

    void fail_maximum_string(const Span span) {
        fail("maximum string size of " + std::to_string(max_runtime_string_bytes) +
                 " bytes exceeded",
             span);
    }

    void execute_jump_if_false(const JumpIfFalseOp& operation, const Span span) {
        if (!require_stack(1U, "JUMP_IF_FALSE", span)) {
            return;
        }
        Value condition = std::move(stack_.back());
        stack_.pop_back();
        const auto* boolean = std::get_if<bool>(&condition);
        if (boolean == nullptr) {
            fail_malformed("conditional jump requires Bool", span);
        } else if (!*boolean) {
            set_instruction_pointer(operation.target, span);
        }
    }

    void execute_jump(const JumpOp& operation, const Span span) {
        set_instruction_pointer(operation.target, span);
    }

    void set_instruction_pointer(const InstructionId target, const Span span) {
        const Chunk* chunk = current_chunk(span);
        const auto index = static_cast<std::size_t>(target.value);
        if (chunk == nullptr || index >= chunk->instructions.size()) {
            if (!diagnostic_) {
                fail_malformed("jump target is out of range", span);
            }
            return;
        }
        frames_.back().instruction_pointer = index;
    }

    void execute_call(const CallOp& operation, const Span span) {
        if (frames_.empty()) {
            fail_malformed("CALL requires a call frame", span);
            return;
        }
        const std::size_t argument_count = static_cast<std::size_t>(operation.argument_count);
        if (argument_count == std::numeric_limits<std::size_t>::max()) {
            fail_malformed("call argument count overflowed", span);
            return;
        }
        const std::size_t required = argument_count + 1U;
        if (!require_stack(required, "CALL", span)) {
            return;
        }
        const std::size_t callee_index = stack_.size() - required;
        if (callee_index < frames_.back().stack_base) {
            fail_malformed("CALL read below the current frame's stack base", span);
            return;
        }

        const Value callee = stack_[callee_index];
        if (const auto* builtin = std::get_if<BuiltinValue>(&callee)) {
            execute_builtin(*builtin, argument_count, callee_index, span);
        } else if (const auto* function = std::get_if<FunctionValue>(&callee)) {
            execute_user_call(*function, argument_count, callee_index, span);
        } else {
            fail("value of type " + std::string{value_type_name(callee)} + " is not callable",
                 span);
        }
    }

    void execute_builtin(const BuiltinValue builtin, const std::size_t argument_count,
                         const std::size_t callee_index, const Span span) {
        if (builtin.builtin == BuiltinCode::frameout || builtin.builtin == BuiltinCode::print) {
            if (argument_count != 1U || callee_index + 1U >= stack_.size()) {
                fail_malformed("builtin output expects exactly one argument", span);
                return;
            }
            const std::string formatted = format_value(stack_[callee_index + 1U], module_);
            if (formatted.size() > max_buffered_output_bytes - output_payload_bytes_) {
                fail("maximum buffered output size of " +
                         std::to_string(max_buffered_output_bytes) + " bytes exceeded",
                     span);
                return;
            }
            const std::size_t appended_size = formatted.size() + 1U;
            if (output_.size() > output_.max_size() - appended_size) {
                fail("unable to reserve output memory", span);
                return;
            }
            try {
                output_.reserve(output_.size() + appended_size);
                output_.append(formatted);
                output_.push_back('\n');
            } catch (const std::bad_alloc&) {
                fail("unable to reserve output memory", span);
                return;
            }
            output_payload_bytes_ += formatted.size();
            stack_.resize(callee_index);
            push(Value{UnitValue{}}, span);
            return;
        }

        if (builtin.builtin == BuiltinCode::framein) {
            if (argument_count != 0U) {
                fail_malformed("builtin input expects no arguments", span);
                return;
            }
            auto line = read_input(span);
            if (!line) {
                return;
            }
            stack_.resize(callee_index);
            push(Value{std::move(*line)}, span);
            return;
        }
        fail_malformed("unknown builtin", span);
    }

    [[nodiscard]] std::optional<std::string> read_input(const Span span) {
        std::string line;
        bool consumed_byte = false;
        bool ended_with_lf = false;

        const auto handle_end_of_input = [&]() {
            if (input_.bad() || (!input_.eof() && input_.fail())) {
                fail("could not read from standard input", span);
                return false;
            }
            if (!consumed_byte) {
                fail("framein reached end of input", span);
                return false;
            }
            return true;
        };

        while (true) {
            std::istream::int_type next = std::char_traits<char>::eof();
            try {
                next = input_.get();
            } catch (const std::ios_base::failure&) {
                if (!handle_end_of_input()) {
                    return std::nullopt;
                }
                break;
            } catch (const std::exception&) {
                fail("could not read from standard input", span);
                return std::nullopt;
            }
            if (next == std::char_traits<char>::eof()) {
                if (!handle_end_of_input()) {
                    return std::nullopt;
                }
                break;
            }

            consumed_byte = true;
            if (total_input_bytes_ >= max_total_input_bytes) {
                fail("maximum input size of " + std::to_string(max_total_input_bytes) +
                         " bytes exceeded",
                     span);
                return std::nullopt;
            }
            ++total_input_bytes_;

            const char byte = static_cast<char>(next);
            if (byte == '\n') {
                ended_with_lf = true;
                break;
            }
            line.push_back(byte);
            if (line.size() > max_input_line_bytes &&
                !(line.size() == max_input_line_bytes + 1U && byte == '\r')) {
                fail("maximum input line size of " + std::to_string(max_input_line_bytes) +
                         " bytes exceeded",
                     span);
                return std::nullopt;
            }
        }

        if (ended_with_lf && !line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() > max_input_line_bytes) {
            fail("maximum input line size of " + std::to_string(max_input_line_bytes) +
                     " bytes exceeded",
                 span);
            return std::nullopt;
        }
        if (!detail::is_valid_utf8(line)) {
            fail("framein read invalid UTF-8", span);
            return std::nullopt;
        }
        return line;
    }

    void execute_user_call(const FunctionValue callee, const std::size_t argument_count,
                           const std::size_t callee_index, const Span span) {
        const auto function_index = static_cast<std::size_t>(callee.function.value);
        if (function_index >= module_.functions.size()) {
            fail_malformed("function index is out of range", span);
            return;
        }
        const auto& function = module_.functions[function_index];
        if (argument_count != function.parameters.size()) {
            fail_malformed("function call has the wrong argument count", span);
            return;
        }
        const std::size_t user_call_depth = frames_.size() - 1U;
        if (user_call_depth >= max_user_call_depth) {
            fail("maximum function call depth exceeded", span);
            return;
        }
        if (initialized_globals_ != globals_.size()) {
            fail_malformed("user function called before all globals were initialized", span);
            return;
        }

        Scope parameters;
        parameters.reserve(function.parameters.size());
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            const std::size_t argument_index = callee_index + 1U + index;
            if (argument_index >= stack_.size()) {
                fail_malformed("function argument is missing", span);
                return;
            }
            parameters.emplace(function.parameters[index].name,
                               RuntimeBinding{std::move(stack_[argument_index]), false});
        }
        stack_.resize(callee_index);

        CallFrame frame;
        frame.function = callee.function;
        frame.instruction_pointer = 0U;
        frame.stack_base = callee_index;
        frame.scopes.push_back(std::move(parameters));
        frame.scope_floor = 1U;
        frame.fallback_span = function.span;
        frames_.push_back(std::move(frame));
        maximum_user_call_depth_ = std::max(maximum_user_call_depth_, frames_.size() - 1U);
    }

    void execute_return(const Span span) {
        if (!require_stack(1U, "RETURN", span) || frames_.empty()) {
            return;
        }
        const bool returning_from_main = !frames_.back().function;
        const std::size_t stack_base = frames_.back().stack_base;
        if (stack_base > stack_.size()) {
            fail_malformed("return stack base is out of range", span);
            return;
        }
        if (returning_from_main) {
            if (frames_.size() != 1U || !frames_.back().scopes.empty() ||
                initialized_globals_ != globals_.size() || stack_.size() != stack_base + 1U ||
                !std::holds_alternative<UnitValue>(stack_.back())) {
                fail_malformed("main must return exactly one Unit value", span);
                return;
            }
        }

        Value result = std::move(stack_.back());
        frames_.pop_back();
        stack_.resize(stack_base);
        if (returning_from_main) {
            final_value_ = std::move(result);
        } else {
            push(std::move(result), span);
        }
    }

    const BytecodeModule& module_;
    std::istream& input_;
    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::vector<std::optional<RuntimeBinding>> globals_;
    std::unordered_map<std::string, std::size_t> global_ids_;
    std::unordered_map<std::string, FunctionId> function_ids_;
    std::string output_;
    std::optional<Value> final_value_;
    std::optional<Diagnostic> diagnostic_;
    std::size_t initialized_globals_{0};
    std::size_t output_payload_bytes_{0};
    std::size_t total_input_bytes_{0};
    std::size_t executed_instruction_count_{0};
    std::size_t maximum_stack_depth_{0};
    std::size_t maximum_user_call_depth_{0};
};

} // namespace

VmResult Vm::run(const BytecodeModule& module, std::istream& input) const {
    VerificationResult verification = BytecodeVerifier{}.verify(module);
    if (const auto* diagnostic = std::get_if<Diagnostic>(&verification)) {
        return VmFailure{*diagnostic, {}};
    }
    if (verification.valueless_by_exception()) {
        return VmFailure{Diagnostic{DiagnosticSeverity::error,
                                    "bytecode verification produced no result", Span{}},
                         {}};
    }
    return Execution{module, input}.run();
}

} // namespace framestepp
