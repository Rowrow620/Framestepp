#include "framestepp/verifier.hpp"

#include "framestepp/type.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

struct Binding final {
    Type type;
    bool mutable_binding{false};
};

using Scope = std::unordered_map<std::string, Binding>;

struct State final {
    std::vector<Type> stack;
    std::vector<Scope> scopes;
    std::size_t initialized_globals{0};
};

struct BindingView final {
    const Type* type{nullptr};
    bool mutable_binding{false};
};

struct ChunkContext final {
    bool main_chunk{false};
    std::size_t scope_floor{0};
    const Type* result_type{nullptr};
    std::string label;
    Span fallback_span;
};

[[nodiscard]] bool binding_equal(const Binding& left, const Binding& right) noexcept {
    return left.mutable_binding == right.mutable_binding && left.type == right.type;
}

[[nodiscard]] bool scope_equal(const Scope& left, const Scope& right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (const auto& [name, binding] : left) {
        const auto other = right.find(name);
        if (other == right.end() || !binding_equal(binding, other->second)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool state_equal(const State& left, const State& right) noexcept {
    if (left.initialized_globals != right.initialized_globals ||
        left.stack.size() != right.stack.size() || left.scopes.size() != right.scopes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.stack.size(); ++index) {
        if (left.stack[index] != right.stack[index]) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.scopes.size(); ++index) {
        if (!scope_equal(left.scopes[index], right.scopes[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_source_type(const Type& type) noexcept {
    return type.kind() == TypeKind::int_ || type.kind() == TypeKind::bool_ ||
           type.kind() == TypeKind::string || type.kind() == TypeKind::unit;
}

[[nodiscard]] bool is_equality_type(const Type& type) noexcept { return is_source_type(type); }

[[nodiscard]] Span safe_span(const BytecodeModule& module, const Span span) noexcept {
    const std::size_t start = std::min(span.start, module.source_size);
    const std::size_t requested_end = span.is_valid() ? span.end : span.start;
    const std::size_t end = std::clamp(requested_end, start, module.source_size);
    return Span{start, end};
}

class Verification final {
  public:
    explicit Verification(const BytecodeModule& module) : module_{module} {
        prelude_.emplace("frameout", Binding{Type::builtin_output(), false});
        prelude_.emplace("print", Binding{Type::builtin_output(), false});
        prelude_.emplace("framein", Binding{Type::builtin_input(), false});
    }

    [[nodiscard]] VerificationResult run() {
        if (auto diagnostic = preflight()) {
            return *diagnostic;
        }

        State main_state;
        const ChunkContext main_context{true, 0U, nullptr, "main", Span{0U, 0U}};
        if (auto diagnostic = verify_chunk(module_.main, main_context, std::move(main_state))) {
            return *diagnostic;
        }
        for (std::size_t index = 0; index < global_types_.size(); ++index) {
            if (!global_types_[index]) {
                return make_error("global `" + module_.globals[index].name +
                                      "` was not initialized by main bytecode",
                                  module_.globals[index].span);
            }
        }

        for (const auto& function : module_.functions) {
            State function_state;
            Scope parameters;
            parameters.reserve(function.parameters.size());
            for (const auto& parameter : function.parameters) {
                parameters.emplace(parameter.name, Binding{parameter.type, false});
            }
            function_state.scopes.push_back(std::move(parameters));
            function_state.initialized_globals = module_.globals.size();
            observe(function_state);

            const ChunkContext function_context{false, 1U, &function.result_type,
                                                "function `" + function.name + "`", function.span};
            if (auto diagnostic =
                    verify_chunk(function.chunk, function_context, std::move(function_state))) {
                return *diagnostic;
            }
        }

        return VerificationSummary{instruction_count_, maximum_stack_depth_, maximum_scope_depth_};
    }

  private:
    [[nodiscard]] Diagnostic make_error(std::string message, const Span span) const {
        return Diagnostic{DiagnosticSeverity::error, std::move(message), safe_span(module_, span)};
    }

    [[nodiscard]] std::optional<Diagnostic> validate_span(const Span span,
                                                          const std::string& owner) const {
        if (!span.is_valid() || span.end > module_.source_size) {
            return make_error(owner + " has a source span outside the module", span);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> preflight() {
        if (module_.constants.size() > max_bytecode_constants) {
            return make_error("bytecode constant limit exceeded", Span{});
        }
        if (module_.globals.size() > max_bytecode_globals) {
            return make_error("bytecode global limit exceeded", Span{});
        }
        if (module_.functions.size() > max_bytecode_functions) {
            return make_error("bytecode function limit exceeded", Span{});
        }

        global_types_.resize(module_.globals.size());
        global_ids_.reserve(module_.globals.size());
        function_types_.reserve(module_.functions.size());

        for (const auto& constant : module_.constants) {
            if (constant.valueless_by_exception()) {
                return make_error("constant table contains a valueless entry", Span{});
            }
            if (const auto* text = std::get_if<std::string>(&constant);
                text != nullptr && !detail::is_valid_utf8(*text)) {
                return make_error("string constant is not valid UTF-8", Span{});
            }
        }

        std::unordered_set<std::string> global_names;
        global_names.reserve(module_.globals.size());
        for (std::size_t index = 0; index < module_.globals.size(); ++index) {
            const auto& global = module_.globals[index];
            if (global.name.empty()) {
                return make_error("global names cannot be empty", global.span);
            }
            if (auto diagnostic = validate_span(global.span, "global `" + global.name + "`")) {
                return diagnostic;
            }
            if (!global_names.insert(global.name).second) {
                return make_error("global `" + global.name + "` is declared more than once",
                                  global.span);
            }
            global_ids_.emplace(global.name, index);
        }

        std::unordered_set<std::string> function_names;
        function_names.reserve(module_.functions.size());
        for (const auto& function : module_.functions) {
            if (function.name.empty()) {
                return make_error("function names cannot be empty", function.span);
            }
            if (auto diagnostic =
                    validate_span(function.span, "function `" + function.name + "`")) {
                return diagnostic;
            }
            if (!function_names.insert(function.name).second) {
                return make_error("function `" + function.name + "` is declared more than once",
                                  function.span);
            }
            if (global_names.contains(function.name)) {
                return make_error("function and global cannot share the name `" + function.name +
                                      "`",
                                  function.span);
            }
            if (function.parameters.size() > max_bytecode_stack_depth) {
                return make_error("function `" + function.name + "` has too many parameters",
                                  function.span);
            }
            if (!is_source_type(function.result_type)) {
                return make_error("function `" + function.name + "` has an invalid result type",
                                  function.span);
            }

            std::unordered_set<std::string> parameter_names;
            parameter_names.reserve(function.parameters.size());
            std::vector<Type> parameter_types;
            parameter_types.reserve(function.parameters.size());
            for (const auto& parameter : function.parameters) {
                if (parameter.name.empty()) {
                    return make_error("parameter names cannot be empty", parameter.span);
                }
                if (auto diagnostic = validate_span(parameter.span, "parameter `" + parameter.name +
                                                                        "` in function `" +
                                                                        function.name + "`")) {
                    return diagnostic;
                }
                if (!parameter_names.insert(parameter.name).second) {
                    return make_error("parameter `" + parameter.name +
                                          "` is declared more than once in function `" +
                                          function.name + "`",
                                      parameter.span);
                }
                if (!is_source_type(parameter.type)) {
                    return make_error("parameter `" + parameter.name +
                                          "` has an invalid bytecode type",
                                      parameter.span);
                }
                parameter_types.push_back(parameter.type);
            }
            function_types_.emplace(
                function.name,
                Type::function(FunctionType{std::move(parameter_types), function.result_type}));
        }

        if (auto diagnostic = preflight_chunk(module_.main, "main", Span{}, true)) {
            return diagnostic;
        }
        for (const auto& function : module_.functions) {
            if (auto diagnostic = preflight_chunk(
                    function.chunk, "function `" + function.name + "`", function.span, false)) {
                return diagnostic;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> preflight_chunk(const Chunk& chunk,
                                                            const std::string& label,
                                                            const Span fallback_span,
                                                            const bool main_chunk) {
        if (chunk.instructions.empty()) {
            return make_error(label + " bytecode must contain at least one instruction",
                              fallback_span);
        }
        if (chunk.instructions.size() > max_chunk_instructions) {
            return make_error(label + " instruction limit exceeded", fallback_span);
        }
        if (instruction_count_ > max_module_instructions - chunk.instructions.size()) {
            return make_error("bytecode module instruction limit exceeded", fallback_span);
        }
        instruction_count_ += chunk.instructions.size();

        std::size_t return_count = 0U;
        for (std::size_t offset = 0; offset < chunk.instructions.size(); ++offset) {
            const auto& instruction = chunk.instructions[offset];
            if (auto diagnostic = validate_span(instruction.span,
                                                label + " instruction " + std::to_string(offset))) {
                return diagnostic;
            }
            if (instruction.operation.valueless_by_exception()) {
                return make_error(label + " contains a valueless operation", instruction.span);
            }

            if (const auto* constant = std::get_if<ConstantOp>(&instruction.operation)) {
                if (static_cast<std::size_t>(constant->constant.value) >=
                    module_.constants.size()) {
                    return make_error("constant index " + std::to_string(constant->constant.value) +
                                          " is out of range",
                                      instruction.span);
                }
            } else if (const auto* initialize =
                           std::get_if<InitializeGlobalOp>(&instruction.operation)) {
                if (!main_chunk) {
                    return make_error("global initialization is only valid in main bytecode",
                                      instruction.span);
                }
                if (static_cast<std::size_t>(initialize->global.value) >= module_.globals.size()) {
                    return make_error("global index " + std::to_string(initialize->global.value) +
                                          " is out of range",
                                      instruction.span);
                }
            } else if (const auto* conditional_jump =
                           std::get_if<JumpIfFalseOp>(&instruction.operation)) {
                if (auto diagnostic =
                        validate_jump(*conditional_jump, chunk, offset, instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* direct_jump = std::get_if<JumpOp>(&instruction.operation)) {
                if (auto diagnostic =
                        validate_jump(*direct_jump, chunk, offset, instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* unary = std::get_if<UnaryOp>(&instruction.operation)) {
                if (unary->operation != UnaryCode::negate && unary->operation != UnaryCode::not_) {
                    return make_error("invalid unary bytecode operation", instruction.span);
                }
            } else if (const auto* binary = std::get_if<BinaryOp>(&instruction.operation)) {
                if (static_cast<unsigned int>(binary->operation) >
                    static_cast<unsigned int>(BinaryCode::greater_equal)) {
                    return make_error("invalid binary bytecode operation", instruction.span);
                }
            } else if (std::holds_alternative<ReturnOp>(instruction.operation)) {
                ++return_count;
            }

            if (const auto* load = std::get_if<LoadOp>(&instruction.operation);
                load != nullptr && load->name.empty()) {
                return make_error("LOAD requires a non-empty name", instruction.span);
            }
            if (const auto* define = std::get_if<DefineLocalOp>(&instruction.operation);
                define != nullptr && define->name.empty()) {
                return make_error("DEFINE_LOCAL requires a non-empty name", instruction.span);
            }
            if (const auto* assign = std::get_if<AssignOp>(&instruction.operation);
                assign != nullptr && assign->name.empty()) {
                return make_error("ASSIGN requires a non-empty name", instruction.span);
            }
        }

        if (main_chunk && return_count != 1U) {
            return make_error("main bytecode must contain exactly one RETURN", fallback_span);
        }
        return std::nullopt;
    }

    template <typename Jump>
    [[nodiscard]] std::optional<Diagnostic> validate_jump(const Jump& jump, const Chunk& chunk,
                                                          const std::size_t offset,
                                                          const Span span) const {
        const std::size_t target = static_cast<std::size_t>(jump.target.value);
        if (target >= chunk.instructions.size()) {
            return make_error("jump target " + std::to_string(target) + " is out of range", span);
        }
        if (target <= offset) {
            return make_error("jump at instruction " + std::to_string(offset) +
                                  " must target a later instruction",
                              span);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<BindingView> lookup(const State& state, const std::string& name,
                                                    const bool main_chunk) const {
        for (auto scope = state.scopes.crbegin(); scope != state.scopes.crend(); ++scope) {
            const auto binding = scope->find(name);
            if (binding != scope->end()) {
                return BindingView{&binding->second.type, binding->second.mutable_binding};
            }
        }

        const auto global_id = global_ids_.find(name);
        if (global_id != global_ids_.end()) {
            const bool initialized = !main_chunk || global_id->second < state.initialized_globals;
            if (initialized && global_types_[global_id->second]) {
                return BindingView{&*global_types_[global_id->second],
                                   module_.globals[global_id->second].mutable_binding};
            }
        }

        const auto function = function_types_.find(name);
        if (function != function_types_.end()) {
            return BindingView{&function->second, false};
        }
        const auto builtin = prelude_.find(name);
        if (builtin != prelude_.end()) {
            return BindingView{&builtin->second.type, false};
        }
        return std::nullopt;
    }

    void observe(const State& state) noexcept {
        maximum_stack_depth_ = std::max(maximum_stack_depth_, state.stack.size());
        maximum_scope_depth_ = std::max(maximum_scope_depth_, state.scopes.size());
    }

    [[nodiscard]] std::optional<Diagnostic> push(State& state, Type type, const Span span) {
        if (state.stack.size() >= max_bytecode_stack_depth) {
            return make_error("maximum bytecode operand stack depth exceeded", span);
        }
        state.stack.push_back(std::move(type));
        observe(state);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> require_stack(const State& state,
                                                          const std::size_t required,
                                                          const std::size_t offset,
                                                          const Span span) const {
        if (state.stack.size() < required) {
            return make_error("operand stack underflow at instruction " + std::to_string(offset) +
                                  ": requires " + std::to_string(required) + " value" +
                                  (required == 1U ? "" : "s") + ", found " +
                                  std::to_string(state.stack.size()),
                              span);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> merge_state(std::vector<std::optional<State>>& incoming,
                                                        const std::size_t target, State state,
                                                        const Chunk& chunk) const {
        if (target >= incoming.size()) {
            return make_error("control-flow target is out of range",
                              chunk.instructions.back().span);
        }
        auto& existing = incoming[target];
        if (!existing) {
            existing = std::move(state);
            return std::nullopt;
        }
        if (!state_equal(*existing, state)) {
            return make_error("inconsistent stack, scope, or global state at instruction " +
                                  std::to_string(target),
                              chunk.instructions[target].span);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic>
    enqueue_fallthrough(std::vector<std::optional<State>>& incoming, const std::size_t offset,
                        State state, const Chunk& chunk) const {
        if (offset + 1U >= chunk.instructions.size()) {
            return make_error("control flow falls off the end after instruction " +
                                  std::to_string(offset),
                              chunk.instructions[offset].span);
        }
        return merge_state(incoming, offset + 1U, std::move(state), chunk);
    }

    [[nodiscard]] std::optional<Diagnostic>
    verify_chunk(const Chunk& chunk, const ChunkContext& context, State initial_state) {
        std::vector<std::optional<State>> incoming(chunk.instructions.size());
        incoming[0] = std::move(initial_state);

        for (std::size_t offset = 0; offset < chunk.instructions.size(); ++offset) {
            if (!incoming[offset]) {
                return make_error("unreachable instruction at offset " + std::to_string(offset),
                                  chunk.instructions[offset].span);
            }
            State state = std::move(*incoming[offset]);
            incoming[offset].reset();
            observe(state);
            const auto& instruction = chunk.instructions[offset];

            if (const auto* constant = std::get_if<ConstantOp>(&instruction.operation)) {
                Type type = Type::unit_type();
                const auto& value = module_.constants[constant->constant.value];
                if (std::holds_alternative<std::int64_t>(value)) {
                    type = Type::int_type();
                } else if (std::holds_alternative<bool>(value)) {
                    type = Type::bool_type();
                } else if (std::holds_alternative<std::string>(value)) {
                    type = Type::string_type();
                } else {
                    return make_error("invalid constant value", instruction.span);
                }
                if (auto diagnostic = push(state, std::move(type), instruction.span)) {
                    return diagnostic;
                }
            } else if (std::holds_alternative<UnitOp>(instruction.operation)) {
                if (auto diagnostic = push(state, Type::unit_type(), instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* load = std::get_if<LoadOp>(&instruction.operation)) {
                const auto binding = lookup(state, load->name, context.main_chunk);
                if (!binding) {
                    return make_error("unknown bytecode binding `" + load->name + "`",
                                      instruction.span);
                }
                if (auto diagnostic = push(state, *binding->type, instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* initialize =
                           std::get_if<InitializeGlobalOp>(&instruction.operation)) {
                if (auto diagnostic = require_stack(state, 1U, offset, instruction.span)) {
                    return diagnostic;
                }
                if (!state.scopes.empty()) {
                    return make_error("global initialization must occur at main scope",
                                      instruction.span);
                }
                const std::size_t global_id = initialize->global.value;
                if (global_id != state.initialized_globals) {
                    return make_error("globals must be initialized exactly once in source order",
                                      instruction.span);
                }
                const Type value_type = state.stack.back();
                auto& recorded_type = global_types_[global_id];
                if (recorded_type && *recorded_type != value_type) {
                    return make_error("global `" + module_.globals[global_id].name +
                                          "` has inconsistent initializer types",
                                      instruction.span);
                }
                if (!recorded_type) {
                    recorded_type = value_type;
                }
                state.stack.pop_back();
                ++state.initialized_globals;
            } else if (const auto* define = std::get_if<DefineLocalOp>(&instruction.operation)) {
                if (auto diagnostic = require_stack(state, 1U, offset, instruction.span)) {
                    return diagnostic;
                }
                if (state.scopes.size() <= context.scope_floor) {
                    return make_error("local definition requires an entered lexical scope",
                                      instruction.span);
                }
                auto& scope = state.scopes.back();
                if (scope.contains(define->name)) {
                    return make_error("local `" + define->name +
                                          "` is already defined in this scope",
                                      instruction.span);
                }
                Type value_type = std::move(state.stack.back());
                state.stack.pop_back();
                scope.emplace(define->name,
                              Binding{std::move(value_type), define->mutable_binding});
            } else if (const auto* assign = std::get_if<AssignOp>(&instruction.operation)) {
                if (auto diagnostic = require_stack(state, 1U, offset, instruction.span)) {
                    return diagnostic;
                }
                const auto binding = lookup(state, assign->name, context.main_chunk);
                if (!binding) {
                    return make_error("unknown bytecode binding `" + assign->name + "`",
                                      instruction.span);
                }
                if (!binding->mutable_binding) {
                    return make_error("cannot assign to immutable bytecode binding `" +
                                          assign->name + "`",
                                      instruction.span);
                }
                if (state.stack.back() != *binding->type) {
                    return make_error("assignment to `" + assign->name + "` expects " +
                                          format_type(*binding->type) + ", found " +
                                          format_type(state.stack.back()),
                                      instruction.span);
                }
            } else if (std::holds_alternative<EnterScopeOp>(instruction.operation)) {
                if (state.scopes.size() >= max_bytecode_scope_depth) {
                    return make_error("maximum bytecode lexical scope depth exceeded",
                                      instruction.span);
                }
                state.scopes.emplace_back();
                observe(state);
            } else if (std::holds_alternative<ExitScopeOp>(instruction.operation)) {
                if (state.scopes.size() <= context.scope_floor) {
                    return make_error("cannot exit a bytecode frame's base scope",
                                      instruction.span);
                }
                state.scopes.pop_back();
            } else if (std::holds_alternative<PopOp>(instruction.operation)) {
                if (auto diagnostic = require_stack(state, 1U, offset, instruction.span)) {
                    return diagnostic;
                }
                state.stack.pop_back();
            } else if (const auto* unary = std::get_if<UnaryOp>(&instruction.operation)) {
                if (auto diagnostic = check_unary(state, *unary, offset, instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* binary = std::get_if<BinaryOp>(&instruction.operation)) {
                if (auto diagnostic = check_binary(state, *binary, offset, instruction.span)) {
                    return diagnostic;
                }
            } else if (const auto* conditional =
                           std::get_if<JumpIfFalseOp>(&instruction.operation)) {
                if (auto diagnostic = require_stack(state, 1U, offset, instruction.span)) {
                    return diagnostic;
                }
                if (state.stack.back().kind() != TypeKind::bool_) {
                    return make_error("conditional jump requires Bool, found " +
                                          format_type(state.stack.back()),
                                      instruction.span);
                }
                state.stack.pop_back();
                State branch_state = state;
                if (auto diagnostic =
                        enqueue_fallthrough(incoming, offset, std::move(state), chunk)) {
                    return diagnostic;
                }
                if (auto diagnostic = merge_state(incoming, conditional->target.value,
                                                  std::move(branch_state), chunk)) {
                    return diagnostic;
                }
                continue;
            } else if (const auto* jump = std::get_if<JumpOp>(&instruction.operation)) {
                if (auto diagnostic =
                        merge_state(incoming, jump->target.value, std::move(state), chunk)) {
                    return diagnostic;
                }
                continue;
            } else if (const auto* call = std::get_if<CallOp>(&instruction.operation)) {
                if (auto diagnostic = check_call(state, *call, context, offset, instruction.span)) {
                    return diagnostic;
                }
            } else if (std::holds_alternative<ReturnOp>(instruction.operation)) {
                if (auto diagnostic = check_return(state, context, offset, instruction.span)) {
                    return diagnostic;
                }
                continue;
            } else {
                return make_error("unknown bytecode operation", instruction.span);
            }

            observe(state);
            if (auto diagnostic = enqueue_fallthrough(incoming, offset, std::move(state), chunk)) {
                return diagnostic;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> check_unary(State& state, const UnaryOp& unary,
                                                        const std::size_t offset,
                                                        const Span span) const {
        if (auto diagnostic = require_stack(state, 1U, offset, span)) {
            return diagnostic;
        }
        const Type expected =
            unary.operation == UnaryCode::negate ? Type::int_type() : Type::bool_type();
        if (state.stack.back() != expected) {
            return make_error("unary bytecode operation requires " + format_type(expected) +
                                  ", found " + format_type(state.stack.back()),
                              span);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic>
    check_binary(State& state, const BinaryOp& binary, const std::size_t offset, const Span span) {
        if (auto diagnostic = require_stack(state, 2U, offset, span)) {
            return diagnostic;
        }
        const Type& left = state.stack[state.stack.size() - 2U];
        const Type& right = state.stack.back();
        Type result = Type::unit_type();
        bool valid = false;

        switch (binary.operation) {
        case BinaryCode::add:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                result = Type::int_type();
                valid = true;
            } else if (left.kind() == TypeKind::string && right.kind() == TypeKind::string) {
                result = Type::string_type();
                valid = true;
            }
            break;
        case BinaryCode::subtract:
        case BinaryCode::multiply:
        case BinaryCode::divide:
        case BinaryCode::remainder:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                result = Type::int_type();
                valid = true;
            }
            break;
        case BinaryCode::equal:
        case BinaryCode::not_equal:
            if (left == right && is_equality_type(left)) {
                result = Type::bool_type();
                valid = true;
            }
            break;
        case BinaryCode::less:
        case BinaryCode::less_equal:
        case BinaryCode::greater:
        case BinaryCode::greater_equal:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                result = Type::bool_type();
                valid = true;
            }
            break;
        }

        if (!valid) {
            return make_error("binary bytecode operation is not defined for " + format_type(left) +
                                  " and " + format_type(right),
                              span);
        }
        state.stack.pop_back();
        state.stack.pop_back();
        state.stack.push_back(std::move(result));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> check_call(State& state, const CallOp& call,
                                                       const ChunkContext& context,
                                                       const std::size_t offset, const Span span) {
        const std::size_t argument_count = call.argument_count;
        if (state.stack.size() <= argument_count) {
            return make_error("operand stack underflow at instruction " + std::to_string(offset) +
                                  ": CALL requires a callee and " + std::to_string(argument_count) +
                                  " arguments, found " + std::to_string(state.stack.size()) +
                                  " values",
                              span);
        }

        const std::size_t callee_index = state.stack.size() - argument_count - 1U;
        const Type callee_type = state.stack[callee_index];
        Type result = Type::unit_type();

        if (callee_type.kind() == TypeKind::builtin_output) {
            if (argument_count != 1U) {
                return make_error("builtin output expects 1 argument, found " +
                                      std::to_string(argument_count),
                                  span);
            }
            result = Type::unit_type();
        } else if (callee_type.kind() == TypeKind::builtin_input) {
            if (argument_count != 0U) {
                return make_error("builtin input expects 0 arguments, found " +
                                      std::to_string(argument_count),
                                  span);
            }
            result = Type::string_type();
        } else if (callee_type.kind() == TypeKind::function) {
            const FunctionType* signature = callee_type.function_type();
            if (signature == nullptr) {
                return make_error("call refers to a malformed function type", span);
            }
            if (argument_count != signature->parameters.size()) {
                return make_error("function call expects " +
                                      std::to_string(signature->parameters.size()) +
                                      " arguments, found " + std::to_string(argument_count),
                                  span);
            }
            for (std::size_t index = 0; index < argument_count; ++index) {
                const Type& actual = state.stack[callee_index + 1U + index];
                if (actual != signature->parameters[index]) {
                    return make_error("call argument " + std::to_string(index + 1U) + " expects " +
                                          format_type(signature->parameters[index]) + ", found " +
                                          format_type(actual),
                                      span);
                }
            }
            if (context.main_chunk && state.initialized_globals != module_.globals.size()) {
                return make_error("main bytecode cannot call a user function before all globals "
                                  "are initialized",
                                  span);
            }
            result = signature->result;
        } else {
            return make_error("value of type " + format_type(callee_type) + " is not callable",
                              span);
        }

        state.stack.erase(state.stack.begin() + static_cast<std::ptrdiff_t>(callee_index),
                          state.stack.end());
        state.stack.push_back(std::move(result));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Diagnostic> check_return(const State& state,
                                                         const ChunkContext& context,
                                                         const std::size_t offset,
                                                         const Span span) const {
        if (auto diagnostic = require_stack(state, 1U, offset, span)) {
            return diagnostic;
        }
        if (context.main_chunk) {
            if (state.initialized_globals != module_.globals.size()) {
                return make_error("main bytecode returned before all globals were initialized",
                                  span);
            }
            if (!state.scopes.empty()) {
                return make_error("main bytecode cannot return with active lexical scopes", span);
            }
            if (state.stack.size() != 1U || state.stack.back().kind() != TypeKind::unit) {
                return make_error("main bytecode must return exactly one Unit value", span);
            }
            return std::nullopt;
        }

        if (context.result_type == nullptr) {
            return make_error("function bytecode is missing a result type", span);
        }
        if (state.stack.back() != *context.result_type) {
            return make_error(context.label + " returns " + format_type(state.stack.back()) +
                                  ", expected " + format_type(*context.result_type),
                              span);
        }
        return std::nullopt;
    }

    const BytecodeModule& module_;
    std::unordered_map<std::string, std::size_t> global_ids_;
    std::vector<std::optional<Type>> global_types_;
    std::unordered_map<std::string, Type> function_types_;
    Scope prelude_;
    std::size_t instruction_count_{0};
    std::size_t maximum_stack_depth_{0};
    std::size_t maximum_scope_depth_{0};
};

} // namespace

VerificationResult BytecodeVerifier::verify(const BytecodeModule& module) const {
    return Verification{module}.run();
}

} // namespace framestepp
