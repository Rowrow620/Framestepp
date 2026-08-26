#include "framestepp/compiler.hpp"

#include "framestepp/type_checker.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace framestepp {
namespace {

enum class Flow {
    continues,
    returns,
};

class Compilation final {
  public:
    explicit Compilation(const Program& program) : program_{program} {
        module_.source_size = program.span.end;
    }

    [[nodiscard]] CompileResult compile() {
        if (!program_.span.is_valid()) {
            return error("cannot compile an invalid program span", program_.span);
        }
        if (!collect_metadata()) {
            return *diagnostic_;
        }

        chunk_ = &module_.main;
        std::size_t function_index = 0U;
        for (const auto& statement : program_.statements) {
            if (const auto* function = std::get_if<Function>(&statement.kind)) {
                if (!compile_function(*function, statement.span, function_index)) {
                    return *diagnostic_;
                }
                ++function_index;
                chunk_ = &module_.main;
                continue;
            }

            if (compile_statement(statement, true) == Flow::returns) {
                if (diagnostic_) {
                    return *diagnostic_;
                }
                return error("top-level code cannot return", statement.span);
            }
        }

        if (!emit(UnitOp{}, program_.span) || !emit(ReturnOp{}, program_.span)) {
            return *diagnostic_;
        }
        return std::move(module_);
    }

  private:
    [[nodiscard]] static Diagnostic error(std::string message, const Span span) {
        return Diagnostic{DiagnosticSeverity::error, std::move(message), span};
    }

    void fail(std::string message, const Span span) {
        if (!diagnostic_) {
            diagnostic_ = error(std::move(message), span);
        }
    }

    [[nodiscard]] bool collect_metadata() {
        for (const auto& statement : program_.statements) {
            if (const auto* function = std::get_if<Function>(&statement.kind)) {
                if (module_.functions.size() >= max_bytecode_functions) {
                    fail("bytecode function limit exceeded", statement.span);
                    return false;
                }

                std::vector<CompiledParameter> parameters;
                parameters.reserve(function->parameters.size());
                for (const auto& parameter : function->parameters) {
                    auto type = resolve_type(parameter.type_name);
                    if (!type) {
                        return false;
                    }
                    parameters.push_back(
                        CompiledParameter{parameter.name.text, std::move(*type), parameter.span});
                }

                Type result_type = Type::unit_type();
                if (function->return_type) {
                    auto resolved = resolve_type(*function->return_type);
                    if (!resolved) {
                        return false;
                    }
                    result_type = std::move(*resolved);
                }
                module_.functions.push_back(
                    CompiledFunction{function->name.text, std::move(parameters),
                                     std::move(result_type), statement.span, Chunk{}});
                continue;
            }

            if (const auto* declaration = std::get_if<LetStatement>(&statement.kind)) {
                if (module_.globals.size() >= max_bytecode_globals) {
                    fail("bytecode global limit exceeded", statement.span);
                    return false;
                }
                module_.globals.push_back(GlobalBinding{
                    declaration->name.text, declaration->mutable_binding, statement.span});
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<Type> resolve_type(const TypeName& type_name) {
        if (type_name.name.text == "Int") {
            return Type::int_type();
        }
        if (type_name.name.text == "Bool") {
            return Type::bool_type();
        }
        if (type_name.name.text == "String") {
            return Type::string_type();
        }
        if (type_name.name.text == "Unit") {
            return Type::unit_type();
        }
        fail("unknown type `" + type_name.name.text + "`", type_name.name.span);
        return std::nullopt;
    }

    [[nodiscard]] bool compile_function(const Function& function, const Span span,
                                        const std::size_t function_index) {
        if (function_index >= module_.functions.size() || function.body == nullptr) {
            fail("malformed function declaration", span);
            return false;
        }
        if (function.parameters.size() > max_bytecode_stack_depth) {
            fail("function parameter limit exceeded", span);
            return false;
        }

        chunk_ = &module_.functions[function_index].chunk;
        const Flow body_flow = compile_block(*function.body);
        if (diagnostic_) {
            return false;
        }
        if (body_flow == Flow::continues && !emit(ReturnOp{}, function.body->span)) {
            return false;
        }
        return true;
    }

    [[nodiscard]] Flow compile_statement(const Statement& statement, const bool top_level) {
        if (std::holds_alternative<Function>(statement.kind)) {
            fail("function declarations are only allowed at top level", statement.span);
            return Flow::returns;
        }

        if (const auto* declaration = std::get_if<LetStatement>(&statement.kind)) {
            if (compile_expression(declaration->initializer) == Flow::returns) {
                return Flow::returns;
            }
            if (top_level) {
                if (next_global_ >= module_.globals.size()) {
                    fail("missing global bytecode metadata", declaration->name.span);
                    return Flow::returns;
                }
                const auto id = GlobalId{static_cast<std::uint32_t>(next_global_)};
                if (!emit(InitializeGlobalOp{id}, declaration->name.span)) {
                    return Flow::returns;
                }
                ++next_global_;
            } else if (!emit(DefineLocalOp{declaration->name.text, declaration->mutable_binding},
                             declaration->name.span)) {
                return Flow::returns;
            }
            return Flow::continues;
        }

        if (const auto* return_statement = std::get_if<ReturnStatement>(&statement.kind)) {
            if (return_statement->value) {
                if (compile_expression(*return_statement->value) == Flow::returns) {
                    return Flow::returns;
                }
            } else if (!emit(UnitOp{}, statement.span)) {
                return Flow::returns;
            }
            if (!emit(ReturnOp{}, statement.span)) {
                return Flow::returns;
            }
            return Flow::returns;
        }

        const auto& expression = std::get<ExpressionStatement>(statement.kind).expression;
        if (compile_expression(expression) == Flow::returns) {
            return Flow::returns;
        }
        if (!emit(PopOp{}, expression.span)) {
            return Flow::returns;
        }
        return Flow::continues;
    }

    [[nodiscard]] Flow compile_block(const Block& block) {
        if (!emit(EnterScopeOp{}, block.span)) {
            return Flow::returns;
        }
        for (const auto& statement : block.statements) {
            if (compile_statement(statement, false) == Flow::returns) {
                return Flow::returns;
            }
        }

        if (block.tail != nullptr) {
            if (compile_expression(*block.tail) == Flow::returns) {
                return Flow::returns;
            }
        } else if (!emit(UnitOp{}, block.span)) {
            return Flow::returns;
        }

        if (!emit(ExitScopeOp{}, block.span)) {
            return Flow::returns;
        }
        return Flow::continues;
    }

    [[nodiscard]] Flow compile_expression(const Expression& expression) {
        if (const auto* integer = std::get_if<IntegerExpression>(&expression.kind)) {
            return emit_constant(ConstantValue{integer->value}, expression.span);
        }
        if (const auto* string = std::get_if<StringExpression>(&expression.kind)) {
            return emit_constant(ConstantValue{string->value}, expression.span);
        }
        if (const auto* boolean = std::get_if<BooleanExpression>(&expression.kind)) {
            return emit_constant(ConstantValue{boolean->value}, expression.span);
        }
        if (const auto* identifier = std::get_if<IdentifierExpression>(&expression.kind)) {
            return emit(LoadOp{identifier->name}, expression.span) ? Flow::continues
                                                                   : Flow::returns;
        }
        if (const auto* group = std::get_if<GroupExpression>(&expression.kind)) {
            if (group->expression == nullptr) {
                fail("malformed grouped expression", expression.span);
                return Flow::returns;
            }
            return compile_expression(*group->expression);
        }
        if (const auto* block = std::get_if<BlockExpression>(&expression.kind)) {
            if (block->block == nullptr) {
                fail("malformed block expression", expression.span);
                return Flow::returns;
            }
            return compile_block(*block->block);
        }
        if (const auto* unary = std::get_if<UnaryExpression>(&expression.kind)) {
            if (unary->operand == nullptr) {
                fail("malformed unary expression", expression.span);
                return Flow::returns;
            }
            if (compile_expression(*unary->operand) == Flow::returns) {
                return Flow::returns;
            }
            const UnaryCode operation =
                unary->operator_kind == UnaryOperator::negate ? UnaryCode::negate : UnaryCode::not_;
            return emit(UnaryOp{operation}, unary->operator_span) ? Flow::continues : Flow::returns;
        }
        if (const auto* binary = std::get_if<BinaryExpression>(&expression.kind)) {
            if (binary->left == nullptr || binary->right == nullptr) {
                fail("malformed binary expression", expression.span);
                return Flow::returns;
            }
            if (compile_expression(*binary->left) == Flow::returns ||
                compile_expression(*binary->right) == Flow::returns) {
                return Flow::returns;
            }
            return emit(BinaryOp{binary_code(binary->operator_kind)}, binary->operator_span)
                       ? Flow::continues
                       : Flow::returns;
        }
        if (const auto* assignment = std::get_if<AssignmentExpression>(&expression.kind)) {
            if (assignment->value == nullptr) {
                fail("malformed assignment expression", expression.span);
                return Flow::returns;
            }
            if (compile_expression(*assignment->value) == Flow::returns) {
                return Flow::returns;
            }
            return emit(AssignOp{assignment->name.text}, assignment->operator_span)
                       ? Flow::continues
                       : Flow::returns;
        }
        if (const auto* call = std::get_if<CallExpression>(&expression.kind)) {
            return compile_call(expression, *call);
        }
        return compile_if(expression, std::get<IfExpression>(expression.kind));
    }

    [[nodiscard]] Flow compile_call(const Expression& expression, const CallExpression& call) {
        if (call.callee == nullptr) {
            fail("malformed call expression", expression.span);
            return Flow::returns;
        }
        if (call.arguments.size() >= max_bytecode_stack_depth) {
            fail("call argument limit exceeded", expression.span);
            return Flow::returns;
        }
        if (compile_expression(*call.callee) == Flow::returns) {
            return Flow::returns;
        }
        for (const auto& argument : call.arguments) {
            if (argument == nullptr) {
                fail("malformed call argument", expression.span);
                return Flow::returns;
            }
            if (compile_expression(*argument) == Flow::returns) {
                return Flow::returns;
            }
        }
        const auto argument_count = static_cast<std::uint32_t>(call.arguments.size());
        return emit(CallOp{argument_count}, expression.span) ? Flow::continues : Flow::returns;
    }

    [[nodiscard]] Flow compile_if(const Expression& expression, const IfExpression& conditional) {
        if (conditional.condition == nullptr || conditional.then_branch == nullptr) {
            fail("malformed conditional expression", expression.span);
            return Flow::returns;
        }
        if (compile_expression(*conditional.condition) == Flow::returns) {
            return Flow::returns;
        }

        auto branch = emit(JumpIfFalseOp{}, conditional.condition->span);
        if (!branch) {
            return Flow::returns;
        }
        const Flow then_flow = compile_block(*conditional.then_branch);
        if (diagnostic_) {
            return Flow::returns;
        }

        std::optional<InstructionId> jump_to_end;
        if (then_flow == Flow::continues) {
            jump_to_end = emit(JumpOp{}, expression.span);
            if (!jump_to_end) {
                return Flow::returns;
            }
        }

        const std::size_t else_start = chunk_->instructions.size();
        Flow else_flow = Flow::continues;
        if (conditional.else_branch != nullptr) {
            else_flow = compile_expression(*conditional.else_branch);
        } else if (!emit(UnitOp{}, expression.span)) {
            return Flow::returns;
        }
        if (diagnostic_ || !patch_jump(*branch, else_start)) {
            return Flow::returns;
        }
        if (jump_to_end && !patch_jump(*jump_to_end, chunk_->instructions.size())) {
            return Flow::returns;
        }
        return then_flow == Flow::returns && else_flow == Flow::returns ? Flow::returns
                                                                        : Flow::continues;
    }

    [[nodiscard]] Flow emit_constant(ConstantValue value, const Span span) {
        if (module_.constants.size() >= max_bytecode_constants) {
            fail("bytecode constant limit exceeded", span);
            return Flow::returns;
        }
        const auto id = ConstantId{static_cast<std::uint32_t>(module_.constants.size())};
        module_.constants.push_back(std::move(value));
        return emit(ConstantOp{id}, span) ? Flow::continues : Flow::returns;
    }

    [[nodiscard]] std::optional<InstructionId> emit(Operation operation, const Span span) {
        if (diagnostic_) {
            return std::nullopt;
        }
        if (!span.is_valid() || span.end > module_.source_size) {
            fail("instruction has an invalid source span", span);
            return std::nullopt;
        }
        if (chunk_ == nullptr) {
            fail("missing bytecode destination", span);
            return std::nullopt;
        }
        if (chunk_->instructions.size() >= max_chunk_instructions) {
            fail("bytecode chunk instruction limit exceeded", span);
            return std::nullopt;
        }
        if (instruction_count_ >= max_module_instructions) {
            fail("bytecode module instruction limit exceeded", span);
            return std::nullopt;
        }
        const auto id = InstructionId{static_cast<std::uint32_t>(chunk_->instructions.size())};
        chunk_->instructions.push_back(Instruction{std::move(operation), span});
        ++instruction_count_;
        return id;
    }

    [[nodiscard]] bool patch_jump(const InstructionId instruction, const std::size_t target) {
        if (chunk_ == nullptr || instruction.value >= chunk_->instructions.size() ||
            target > max_chunk_instructions) {
            fail("cannot patch bytecode jump", program_.span);
            return false;
        }
        const auto target_id = InstructionId{static_cast<std::uint32_t>(target)};
        auto& operation = chunk_->instructions[instruction.value].operation;
        if (auto* jump = std::get_if<JumpOp>(&operation)) {
            jump->target = target_id;
            return true;
        }
        if (auto* branch = std::get_if<JumpIfFalseOp>(&operation)) {
            branch->target = target_id;
            return true;
        }
        fail("cannot patch a non-jump instruction", chunk_->instructions[instruction.value].span);
        return false;
    }

    [[nodiscard]] static BinaryCode binary_code(const BinaryOperator operation) noexcept {
        switch (operation) {
        case BinaryOperator::add:
            return BinaryCode::add;
        case BinaryOperator::subtract:
            return BinaryCode::subtract;
        case BinaryOperator::multiply:
            return BinaryCode::multiply;
        case BinaryOperator::divide:
            return BinaryCode::divide;
        case BinaryOperator::remainder:
            return BinaryCode::remainder;
        case BinaryOperator::equal:
            return BinaryCode::equal;
        case BinaryOperator::not_equal:
            return BinaryCode::not_equal;
        case BinaryOperator::less:
            return BinaryCode::less;
        case BinaryOperator::less_equal:
            return BinaryCode::less_equal;
        case BinaryOperator::greater:
            return BinaryCode::greater;
        case BinaryOperator::greater_equal:
            return BinaryCode::greater_equal;
        }
        return BinaryCode::add;
    }

    const Program& program_;
    BytecodeModule module_;
    Chunk* chunk_{nullptr};
    std::optional<Diagnostic> diagnostic_;
    std::size_t instruction_count_{0};
    std::size_t next_global_{0};
};

} // namespace

CompileResult Compiler::compile(const Program& program) const {
    if (auto diagnostic = TypeChecker{}.check(program)) {
        return *diagnostic;
    }
    return Compilation{program}.compile();
}

} // namespace framestepp
