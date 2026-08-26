#include "framestepp/type_checker.hpp"

#include "framestepp/type.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

using TypeResult = std::variant<Type, Diagnostic>;

[[nodiscard]] Diagnostic error(std::string message, const Span span) {
    return Diagnostic{DiagnosticSeverity::error, std::move(message), span};
}

[[nodiscard]] bool is_assignable(const Type& actual, const Type& expected) noexcept {
    return actual.kind() == TypeKind::never || actual == expected;
}

[[nodiscard]] std::optional<Type> join_branch_types(const Type& left, const Type& right) {
    if (left.kind() == TypeKind::never) {
        return right;
    }
    if (right.kind() == TypeKind::never || left == right) {
        return left;
    }
    return std::nullopt;
}

[[nodiscard]] bool equality_types(const Type& left, const Type& right) noexcept {
    if (left != right) {
        return false;
    }
    return left.kind() == TypeKind::int_ || left.kind() == TypeKind::bool_ ||
           left.kind() == TypeKind::string || left.kind() == TypeKind::unit;
}

[[nodiscard]] std::string_view unary_spelling(const UnaryOperator operation) noexcept {
    switch (operation) {
    case UnaryOperator::negate:
        return "-";
    case UnaryOperator::not_:
        return "!";
    }
    return "?";
}

[[nodiscard]] std::string_view binary_spelling(const BinaryOperator operation) noexcept {
    switch (operation) {
    case BinaryOperator::add:
        return "+";
    case BinaryOperator::subtract:
        return "-";
    case BinaryOperator::multiply:
        return "*";
    case BinaryOperator::divide:
        return "/";
    case BinaryOperator::remainder:
        return "%";
    case BinaryOperator::equal:
        return "==";
    case BinaryOperator::not_equal:
        return "!=";
    case BinaryOperator::less:
        return "<";
    case BinaryOperator::less_equal:
        return "<=";
    case BinaryOperator::greater:
        return ">";
    case BinaryOperator::greater_equal:
        return ">=";
    }
    return "?";
}

[[nodiscard]] std::string arity_message(const std::size_t expected, const std::size_t actual) {
    return "function expected " + std::to_string(expected) + " argument" +
           (expected == 1U ? "" : "s") + ", but received " + std::to_string(actual);
}

struct TypeBinding final {
    Type value_type;
    bool mutable_binding{false};
};

class TypeEnvironment final {
  public:
    TypeEnvironment() {
        scopes_.emplace_back();
        scopes_.back().emplace("frameout", TypeBinding{Type::builtin_output(), false});
        scopes_.back().emplace("print", TypeBinding{Type::builtin_output(), false});
        scopes_.back().emplace("framein", TypeBinding{Type::builtin_input(), false});
        scopes_.emplace_back();
    }

    void push_scope() { scopes_.emplace_back(); }

    void pop_scope() {
        assert(scopes_.size() > 2U && "cannot remove a global type scope");
        scopes_.pop_back();
    }

    [[nodiscard]] TypeCheckResult define_global(const Name& name, Type value_type,
                                                const bool mutable_binding) {
        return define_at(1U, name, std::move(value_type), mutable_binding);
    }

    [[nodiscard]] TypeCheckResult define_current(const Name& name, Type value_type,
                                                 const bool mutable_binding) {
        return define_at(scopes_.size() - 1U, name, std::move(value_type), mutable_binding);
    }

    [[nodiscard]] std::optional<TypeBinding> lookup(const std::string& name) const {
        for (auto scope = scopes_.crbegin(); scope != scopes_.crend(); ++scope) {
            const auto binding = scope->find(name);
            if (binding != scope->end()) {
                return binding->second;
            }
        }
        return std::nullopt;
    }

  private:
    using Scope = std::unordered_map<std::string, TypeBinding>;

    [[nodiscard]] TypeCheckResult define_at(const std::size_t scope, const Name& name,
                                            Type value_type, const bool mutable_binding) {
        auto& bindings = scopes_[scope];
        const bool inserted =
            bindings.emplace(name.text, TypeBinding{std::move(value_type), mutable_binding}).second;
        if (!inserted) {
            return error("name `" + name.text + "` is already defined in this scope", name.span);
        }
        return std::nullopt;
    }

    std::vector<Scope> scopes_;
};

class Checker final {
  public:
    [[nodiscard]] TypeCheckResult check(const Program& program) {
        if (auto diagnostic = collect_function_signatures(program)) {
            return diagnostic;
        }

        bool saw_top_level_expression = false;
        for (const auto& statement : program.statements) {
            if (std::holds_alternative<Function>(statement.kind)) {
                continue;
            }
            if (std::holds_alternative<LetStatement>(statement.kind)) {
                if (saw_top_level_expression) {
                    return error("global variables must be declared before top-level expressions",
                                 statement.span);
                }
                in_global_initializer_ = true;
                auto checked = check_statement(statement);
                in_global_initializer_ = false;
                if (const auto* diagnostic = std::get_if<Diagnostic>(&checked)) {
                    return *diagnostic;
                }
                continue;
            }

            saw_top_level_expression = true;
            auto checked = check_statement(statement);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&checked)) {
                return *diagnostic;
            }
        }

        for (const auto& statement : program.statements) {
            if (const auto* function = std::get_if<Function>(&statement.kind)) {
                if (auto diagnostic = check_function(*function)) {
                    return diagnostic;
                }
            }
        }
        return std::nullopt;
    }

  private:
    [[nodiscard]] TypeCheckResult collect_function_signatures(const Program& program) {
        for (const auto& statement : program.statements) {
            const auto* function = std::get_if<Function>(&statement.kind);
            if (function == nullptr) {
                continue;
            }

            std::unordered_set<std::string> parameter_names;
            std::vector<Type> parameters;
            parameters.reserve(function->parameters.size());
            for (const auto& parameter : function->parameters) {
                if (!parameter_names.insert(parameter.name.text).second) {
                    return error("parameter `" + parameter.name.text +
                                     "` is already declared in this function",
                                 parameter.name.span);
                }
                auto parameter_type = resolve_type_name(parameter.type_name);
                if (const auto* diagnostic = std::get_if<Diagnostic>(&parameter_type)) {
                    return *diagnostic;
                }
                parameters.push_back(std::get<Type>(std::move(parameter_type)));
            }

            Type result = Type::unit_type();
            if (function->return_type) {
                auto resolved = resolve_type_name(*function->return_type);
                if (const auto* diagnostic = std::get_if<Diagnostic>(&resolved)) {
                    return *diagnostic;
                }
                result = std::get<Type>(std::move(resolved));
            }

            FunctionType signature{std::move(parameters), std::move(result)};
            if (auto diagnostic =
                    environment_.define_global(function->name, Type::function(signature), false)) {
                return diagnostic;
            }
            functions_.emplace(function->name.text, std::move(signature));
        }
        return std::nullopt;
    }

    [[nodiscard]] TypeCheckResult check_function(const Function& function) {
        const auto signature_entry = functions_.find(function.name.text);
        if (signature_entry == functions_.end()) {
            return error("missing function signature for `" + function.name.text + "`",
                         function.name.span);
        }
        const FunctionType signature = signature_entry->second;

        environment_.push_scope();
        auto previous_return_type = std::move(current_return_type_);
        current_return_type_ = signature.result;

        TypeCheckResult diagnostic;
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            diagnostic = environment_.define_current(function.parameters[index].name,
                                                     signature.parameters[index], false);
            if (diagnostic) {
                break;
            }
        }

        if (!diagnostic) {
            auto body_type = check_block(*function.body);
            if (const auto* body_diagnostic = std::get_if<Diagnostic>(&body_type)) {
                diagnostic = *body_diagnostic;
            } else {
                const Type& actual = std::get<Type>(body_type);
                if (!is_assignable(actual, signature.result)) {
                    const Span span = function.body->tail != nullptr ? function.body->tail->span
                                                                     : function.body->span;
                    diagnostic =
                        error("function `" + function.name.text + "` must return " +
                                  format_type(signature.result) + ", found " + format_type(actual),
                              span);
                }
            }
        }

        current_return_type_ = std::move(previous_return_type);
        environment_.pop_scope();
        return diagnostic;
    }

    [[nodiscard]] TypeResult check_statement(const Statement& statement) {
        if (std::holds_alternative<Function>(statement.kind)) {
            return error("function declarations are only allowed at top level", statement.span);
        }

        if (const auto* declaration = std::get_if<LetStatement>(&statement.kind)) {
            auto initializer_type = check_expression(declaration->initializer);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&initializer_type)) {
                return *diagnostic;
            }
            const Type actual = std::get<Type>(initializer_type);
            Type binding_type = actual;
            if (declaration->type_annotation) {
                auto resolved = resolve_type_name(*declaration->type_annotation);
                if (const auto* diagnostic = std::get_if<Diagnostic>(&resolved)) {
                    return *diagnostic;
                }
                binding_type = std::get<Type>(std::move(resolved));
            }
            if (!is_assignable(actual, binding_type)) {
                return error("variable `" + declaration->name.text + "` expects " +
                                 format_type(binding_type) + ", found " + format_type(actual),
                             declaration->initializer.span);
            }
            if (auto diagnostic = environment_.define_current(
                    declaration->name, std::move(binding_type), declaration->mutable_binding)) {
                return *diagnostic;
            }
            return actual.kind() == TypeKind::never ? Type::never() : Type::unit_type();
        }

        if (const auto* return_statement = std::get_if<ReturnStatement>(&statement.kind)) {
            if (!current_return_type_) {
                return error("`return` can only be used inside a function", statement.span);
            }
            Type actual = Type::unit_type();
            if (return_statement->value) {
                auto checked = check_expression(*return_statement->value);
                if (const auto* diagnostic = std::get_if<Diagnostic>(&checked)) {
                    return *diagnostic;
                }
                actual = std::get<Type>(std::move(checked));
            }
            if (!is_assignable(actual, *current_return_type_)) {
                const Span span =
                    return_statement->value ? return_statement->value->span : statement.span;
                return error("return expects " + format_type(*current_return_type_) + ", found " +
                                 format_type(actual),
                             span);
            }
            return Type::never();
        }

        const auto& expression = std::get<ExpressionStatement>(statement.kind).expression;
        auto expression_type = check_expression(expression);
        if (const auto* diagnostic = std::get_if<Diagnostic>(&expression_type)) {
            return *diagnostic;
        }
        return std::get<Type>(expression_type).kind() == TypeKind::never ? Type::never()
                                                                         : Type::unit_type();
    }

    [[nodiscard]] TypeResult check_block(const Block& block) {
        environment_.push_scope();
        bool diverges = false;
        TypeResult result = Type::unit_type();

        for (const auto& statement : block.statements) {
            auto statement_type = check_statement(statement);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&statement_type)) {
                result = *diagnostic;
                environment_.pop_scope();
                return result;
            }
            if (!diverges && std::get<Type>(statement_type).kind() == TypeKind::never) {
                diverges = true;
            }
        }

        Type tail_type = Type::unit_type();
        if (block.tail != nullptr) {
            auto checked_tail = check_expression(*block.tail);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&checked_tail)) {
                result = *diagnostic;
                environment_.pop_scope();
                return result;
            }
            tail_type = std::get<Type>(std::move(checked_tail));
        }
        result = diverges ? Type::never() : std::move(tail_type);
        environment_.pop_scope();
        return result;
    }

    [[nodiscard]] TypeResult check_expression(const Expression& expression) {
        if (expression_depth_ >= max_type_check_depth) {
            return error("maximum type-checking expression depth exceeded", expression.span);
        }
        ++expression_depth_;
        auto result = check_expression_inner(expression);
        --expression_depth_;
        return result;
    }

    [[nodiscard]] TypeResult check_expression_inner(const Expression& expression) {
        if (std::holds_alternative<IntegerExpression>(expression.kind)) {
            return Type::int_type();
        }
        if (std::holds_alternative<StringExpression>(expression.kind)) {
            return Type::string_type();
        }
        if (std::holds_alternative<BooleanExpression>(expression.kind)) {
            return Type::bool_type();
        }
        if (const auto* identifier = std::get_if<IdentifierExpression>(&expression.kind)) {
            auto binding = environment_.lookup(identifier->name);
            if (!binding) {
                return error("unknown variable `" + identifier->name + "`", expression.span);
            }
            return binding->value_type;
        }
        if (const auto* group = std::get_if<GroupExpression>(&expression.kind)) {
            return check_expression(*group->expression);
        }
        if (const auto* block = std::get_if<BlockExpression>(&expression.kind)) {
            return check_block(*block->block);
        }
        if (const auto* unary = std::get_if<UnaryExpression>(&expression.kind)) {
            auto operand_type = check_expression(*unary->operand);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&operand_type)) {
                return *diagnostic;
            }
            const Type actual = std::get<Type>(operand_type);
            if (actual.kind() == TypeKind::never) {
                return Type::never();
            }
            const Type expected = unary->operator_kind == UnaryOperator::negate ? Type::int_type()
                                                                                : Type::bool_type();
            if (actual != expected) {
                return error("unary `" + std::string{unary_spelling(unary->operator_kind)} +
                                 "` requires " + format_type(expected) + ", found " +
                                 format_type(actual),
                             unary->operator_span);
            }
            return expected;
        }
        if (const auto* binary = std::get_if<BinaryExpression>(&expression.kind)) {
            auto left = check_expression(*binary->left);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&left)) {
                return *diagnostic;
            }
            auto right = check_expression(*binary->right);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&right)) {
                return *diagnostic;
            }
            const Type left_type = std::get<Type>(left);
            const Type right_type = std::get<Type>(right);
            if (left_type.kind() == TypeKind::never || right_type.kind() == TypeKind::never) {
                return Type::never();
            }
            return check_binary(binary->operator_kind, left_type, right_type,
                                binary->operator_span);
        }
        if (const auto* assignment = std::get_if<AssignmentExpression>(&expression.kind)) {
            auto binding = environment_.lookup(assignment->name.text);
            if (!binding) {
                return error("unknown variable `" + assignment->name.text + "`",
                             assignment->name.span);
            }
            if (!binding->mutable_binding) {
                return error("cannot assign to immutable variable `" + assignment->name.text + "`",
                             assignment->name.span);
            }
            auto value = check_expression(*assignment->value);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&value)) {
                return *diagnostic;
            }
            const Type actual = std::get<Type>(value);
            if (!is_assignable(actual, binding->value_type)) {
                return error("assignment to `" + assignment->name.text + "` expects " +
                                 format_type(binding->value_type) + ", found " +
                                 format_type(actual),
                             assignment->operator_span);
            }
            return actual.kind() == TypeKind::never ? Type::never() : binding->value_type;
        }
        if (const auto* call = std::get_if<CallExpression>(&expression.kind)) {
            return check_call(expression, *call);
        }

        const auto& conditional = std::get<IfExpression>(expression.kind);
        auto condition = check_expression(*conditional.condition);
        if (const auto* diagnostic = std::get_if<Diagnostic>(&condition)) {
            return *diagnostic;
        }
        auto then_branch = check_block(*conditional.then_branch);
        if (const auto* diagnostic = std::get_if<Diagnostic>(&then_branch)) {
            return *diagnostic;
        }
        TypeResult else_branch = Type::unit_type();
        if (conditional.else_branch != nullptr) {
            else_branch = check_expression(*conditional.else_branch);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&else_branch)) {
                return *diagnostic;
            }
        }

        const Type condition_type = std::get<Type>(condition);
        const Type then_type = std::get<Type>(then_branch);
        const Type else_type = std::get<Type>(else_branch);
        if (condition_type.kind() == TypeKind::never) {
            return Type::never();
        }
        if (condition_type.kind() != TypeKind::bool_) {
            return error("`if` condition must be Bool, found " + format_type(condition_type),
                         conditional.condition->span);
        }
        auto joined = join_branch_types(then_type, else_type);
        if (!joined) {
            const Span span = conditional.else_branch != nullptr ? conditional.else_branch->span
                                                                 : conditional.then_branch->span;
            return error("`if` branches have incompatible types: " + format_type(then_type) +
                             " and " + format_type(else_type),
                         span);
        }
        return *joined;
    }

    [[nodiscard]] TypeResult check_call(const Expression& expression, const CallExpression& call) {
        auto checked_callee = check_expression(*call.callee);
        if (const auto* diagnostic = std::get_if<Diagnostic>(&checked_callee)) {
            return *diagnostic;
        }

        std::vector<Type> argument_types;
        argument_types.reserve(call.arguments.size());
        for (const auto& argument : call.arguments) {
            auto checked = check_expression(*argument);
            if (const auto* diagnostic = std::get_if<Diagnostic>(&checked)) {
                return *diagnostic;
            }
            argument_types.push_back(std::get<Type>(std::move(checked)));
        }

        const Type callee_type = std::get<Type>(checked_callee);
        const bool argument_diverges =
            std::any_of(argument_types.begin(), argument_types.end(),
                        [](const Type& type) { return type.kind() == TypeKind::never; });
        if (callee_type.kind() == TypeKind::never || argument_diverges) {
            return Type::never();
        }
        if (callee_type.kind() == TypeKind::builtin_output) {
            if (argument_types.size() != 1U) {
                return error(arity_message(1U, argument_types.size()), expression.span);
            }
            return Type::unit_type();
        }
        if (callee_type.kind() == TypeKind::builtin_input) {
            if (!argument_types.empty()) {
                return error(arity_message(0U, argument_types.size()), expression.span);
            }
            return Type::string_type();
        }
        if (callee_type.kind() != TypeKind::function) {
            return error("value of type " + format_type(callee_type) + " is not callable",
                         call.callee->span);
        }
        if (in_global_initializer_) {
            return error("global initializers cannot call user-defined functions",
                         call.callee->span);
        }

        const FunctionType& signature = *callee_type.function_type();
        if (argument_types.size() != signature.parameters.size()) {
            return error(arity_message(signature.parameters.size(), argument_types.size()),
                         expression.span);
        }
        for (std::size_t index = 0; index < argument_types.size(); ++index) {
            if (!is_assignable(argument_types[index], signature.parameters[index])) {
                return error("argument " + std::to_string(index + 1U) + " expects " +
                                 format_type(signature.parameters[index]) + ", found " +
                                 format_type(argument_types[index]),
                             call.arguments[index]->span);
            }
        }
        return signature.result;
    }

    [[nodiscard]] TypeResult check_binary(const BinaryOperator operation, const Type& left,
                                          const Type& right, const Span span) const {
        switch (operation) {
        case BinaryOperator::add:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                return Type::int_type();
            }
            if (left.kind() == TypeKind::string && right.kind() == TypeKind::string) {
                return Type::string_type();
            }
            break;
        case BinaryOperator::subtract:
        case BinaryOperator::multiply:
        case BinaryOperator::divide:
        case BinaryOperator::remainder:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                return Type::int_type();
            }
            break;
        case BinaryOperator::less:
        case BinaryOperator::less_equal:
        case BinaryOperator::greater:
        case BinaryOperator::greater_equal:
            if (left.kind() == TypeKind::int_ && right.kind() == TypeKind::int_) {
                return Type::bool_type();
            }
            break;
        case BinaryOperator::equal:
        case BinaryOperator::not_equal:
            if (equality_types(left, right)) {
                return Type::bool_type();
            }
            break;
        }
        return error("operator `" + std::string{binary_spelling(operation)} +
                         "` is not defined for " + format_type(left) + " and " + format_type(right),
                     span);
    }

    [[nodiscard]] TypeResult resolve_type_name(const TypeName& type_name) const {
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
        return error("unknown type `" + type_name.name.text + "`", type_name.name.span);
    }

    TypeEnvironment environment_;
    std::unordered_map<std::string, FunctionType> functions_;
    std::optional<Type> current_return_type_;
    std::size_t expression_depth_{0};
    bool in_global_initializer_{false};
};

} // namespace

TypeCheckResult TypeChecker::check(const Program& program) const {
    return Checker{}.check(program);
}

} // namespace framestepp
