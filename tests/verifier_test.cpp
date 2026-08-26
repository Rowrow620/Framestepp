#include "framestepp/compiler.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"
#include "framestepp/verifier.hpp"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

constexpr Span test_span{0U, 1U};

[[nodiscard]] Instruction instruction(Operation operation, const Span span = test_span) {
    return Instruction{std::move(operation), span};
}

[[nodiscard]] Chunk chunk(std::initializer_list<Operation> operations) {
    Chunk result;
    result.instructions.reserve(operations.size());
    for (const auto& operation : operations) {
        result.instructions.push_back(instruction(operation));
    }
    return result;
}

[[nodiscard]] BytecodeModule module_with(Chunk main) {
    return BytecodeModule{1U, {}, {}, std::move(main), {}};
}

[[nodiscard]] Diagnostic verification_error(const BytecodeModule& module) {
    auto result = BytecodeVerifier{}.verify(module);
    const auto* diagnostic = std::get_if<Diagnostic>(&result);
    if (diagnostic == nullptr) {
        ADD_FAILURE() << "expected bytecode verification to fail";
        return Diagnostic{DiagnosticSeverity::error, "missing verification error", Span{}};
    }
    return *diagnostic;
}

[[nodiscard]] VerificationSummary verification_summary(const BytecodeModule& module) {
    auto result = BytecodeVerifier{}.verify(module);
    const auto* summary = std::get_if<VerificationSummary>(&result);
    if (summary == nullptr) {
        ADD_FAILURE() << "expected bytecode verification to succeed, got: "
                      << std::get<Diagnostic>(result).message;
        return VerificationSummary{};
    }
    return *summary;
}

[[nodiscard]] BytecodeModule compile_ok(const std::string_view source) {
    auto lexed = Lexer{source}.lex();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&lexed)) {
        ADD_FAILURE() << "expected lexing to succeed, got: " << diagnostic->message;
        return module_with(chunk({UnitOp{}, ReturnOp{}}));
    }
    auto parsed = Parser{std::move(std::get<std::vector<Token>>(lexed))}.parse();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&parsed)) {
        ADD_FAILURE() << "expected parsing to succeed, got: " << diagnostic->message;
        return module_with(chunk({UnitOp{}, ReturnOp{}}));
    }
    auto compiled = Compiler{}.compile(std::get<Program>(parsed));
    if (const auto* diagnostic = std::get_if<Diagnostic>(&compiled)) {
        ADD_FAILURE() << "expected compilation to succeed, got: " << diagnostic->message;
        return module_with(chunk({UnitOp{}, ReturnOp{}}));
    }
    return std::move(std::get<BytecodeModule>(compiled));
}

TEST(BytecodeVerifierTest, AcceptsTypedCompilerOutputAndReportsMetrics) {
    BytecodeModule module{
        1U,
        {ConstantValue{std::int64_t{41}}, ConstantValue{std::int64_t{1}}},
        {GlobalBinding{"answer", true, test_span}},
        chunk({ConstantOp{ConstantId{0U}}, InitializeGlobalOp{GlobalId{0U}}, LoadOp{"increment"},
               LoadOp{"answer"}, CallOp{1U}, AssignOp{"answer"}, PopOp{}, UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"increment",
                          {CompiledParameter{"value", Type::int_type(), test_span}},
                          Type::int_type(),
                          test_span,
                          chunk({EnterScopeOp{}, LoadOp{"value"}, ConstantOp{ConstantId{1U}},
                                 BinaryOp{BinaryCode::add}, ExitScopeOp{}, ReturnOp{}})}}};

    const auto summary = verification_summary(module);
    EXPECT_EQ(summary.instruction_count, 15U);
    EXPECT_EQ(summary.maximum_stack_depth, 2U);
    EXPECT_EQ(summary.maximum_scope_depth, 2U);

    const auto compiled = compile_ok(R"(
        fn choose(flag: Bool) -> Int { if flag { return 1; }; 2 }
        let mut answer = 0;
        answer = choose(true);
        answer = answer + 1;
        frameout(answer);
    )");
    EXPECT_TRUE(std::holds_alternative<VerificationSummary>(BytecodeVerifier{}.verify(compiled)));
}

TEST(BytecodeVerifierTest, PreservesPreludeIdentityAcrossGlobalShadowing) {
    BytecodeModule module{
        1U,
        {ConstantValue{std::int64_t{7}}},
        {GlobalBinding{"frameout", false, test_span}},
        chunk({LoadOp{"frameout"}, InitializeGlobalOp{GlobalId{0U}}, LoadOp{"frameout"},
               ConstantOp{ConstantId{0U}}, CallOp{1U}, PopOp{}, UnitOp{}, ReturnOp{}}),
        {}};

    EXPECT_TRUE(std::holds_alternative<VerificationSummary>(BytecodeVerifier{}.verify(module)));
}

TEST(BytecodeVerifierTest, RejectsMalformedTablesAndSourceSpans) {
    EXPECT_NE(verification_error(BytecodeModule{}).message.find("at least one instruction"),
              std::string::npos);

    auto bad_span = module_with(
        Chunk{{instruction(UnitOp{}, Span{0U, 2U}), instruction(ReturnOp{}, test_span)}});
    EXPECT_NE(verification_error(bad_span).message.find("source span"), std::string::npos);

    BytecodeModule duplicate_globals{
        1U,
        {},
        {GlobalBinding{"value", false, test_span}, GlobalBinding{"value", true, test_span}},
        chunk({UnitOp{}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(duplicate_globals).message.find("more than once"),
              std::string::npos);

    BytecodeModule conflicting_names{
        1U,
        {},
        {GlobalBinding{"value", false, test_span}},
        chunk({UnitOp{}, InitializeGlobalOp{GlobalId{0U}}, UnitOp{}, ReturnOp{}}),
        {CompiledFunction{
            "value", {}, Type::unit_type(), test_span, chunk({UnitOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(conflicting_names).message.find("share the name"),
              std::string::npos);

    BytecodeModule duplicate_parameters{
        1U,
        {},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"same",
                          {CompiledParameter{"value", Type::int_type(), test_span},
                           CompiledParameter{"value", Type::int_type(), test_span}},
                          Type::unit_type(),
                          test_span,
                          chunk({UnitOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(duplicate_parameters).message.find("parameter `value`"),
              std::string::npos);

    BytecodeModule invalid_type{
        1U,
        {},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"bad", {}, Type::never(), test_span, chunk({UnitOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(invalid_type).message.find("invalid result type"),
              std::string::npos);
}

TEST(BytecodeVerifierTest, ValidatesAllStaticOperandsBeforeReachability) {
    BytecodeModule invalid_constant{
        1U, {}, {}, chunk({UnitOp{}, ReturnOp{}, ConstantOp{ConstantId{99U}}}), {}};
    EXPECT_NE(verification_error(invalid_constant).message.find("constant index 99"),
              std::string::npos);

    BytecodeModule invalid_global{
        1U, {}, {}, chunk({UnitOp{}, InitializeGlobalOp{GlobalId{99U}}, UnitOp{}, ReturnOp{}}), {}};
    EXPECT_NE(verification_error(invalid_global).message.find("global index 99"),
              std::string::npos);

    const auto invalid_function_init =
        BytecodeModule{1U,
                       {},
                       {GlobalBinding{"value", false, test_span}},
                       chunk({UnitOp{}, InitializeGlobalOp{GlobalId{0U}}, UnitOp{}, ReturnOp{}}),
                       {CompiledFunction{"bad",
                                         {},
                                         Type::unit_type(),
                                         test_span,
                                         chunk({UnitOp{}, InitializeGlobalOp{GlobalId{0U}},
                                                UnitOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(invalid_function_init).message.find("only valid in main"),
              std::string::npos);

    auto invalid_unary = module_with(
        chunk({UnitOp{}, UnaryOp{static_cast<UnaryCode>(99)}, PopOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(invalid_unary).message.find("invalid unary"), std::string::npos);
}

TEST(BytecodeVerifierTest, RejectsInvalidAbsoluteJumps) {
    const auto out_of_range = module_with(chunk({JumpOp{InstructionId{9U}}, ReturnOp{}}));
    EXPECT_NE(verification_error(out_of_range).message.find("out of range"), std::string::npos);

    const auto self = module_with(chunk({JumpOp{InstructionId{0U}}, ReturnOp{}}));
    EXPECT_NE(verification_error(self).message.find("later instruction"), std::string::npos);

    const auto backward = module_with(chunk({UnitOp{}, JumpOp{InstructionId{0U}}, ReturnOp{}}));
    EXPECT_NE(verification_error(backward).message.find("later instruction"), std::string::npos);
}

TEST(BytecodeVerifierTest, RejectsStackUnderflowWithoutArithmeticOverflow) {
    const std::vector<BytecodeModule> malformed{
        module_with(chunk({PopOp{}, UnitOp{}, ReturnOp{}})),
        module_with(chunk({UnaryOp{UnaryCode::negate}, UnitOp{}, ReturnOp{}})),
        module_with(chunk({BinaryOp{BinaryCode::add}, UnitOp{}, ReturnOp{}})),
        module_with(
            chunk({CallOp{std::numeric_limits<std::uint32_t>::max()}, UnitOp{}, ReturnOp{}})),
    };

    for (const auto& module : malformed) {
        EXPECT_NE(verification_error(module).message.find("stack underflow"), std::string::npos);
    }
}

TEST(BytecodeVerifierTest, EnforcesOperationTypes) {
    BytecodeModule bad_condition{
        1U,
        {ConstantValue{std::int64_t{1}}},
        {},
        chunk({ConstantOp{ConstantId{0U}}, JumpIfFalseOp{InstructionId{3U}}, UnitOp{}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(bad_condition).message.find("requires Bool"), std::string::npos);

    BytecodeModule bad_add{1U,
                           {ConstantValue{true}, ConstantValue{std::int64_t{1}}},
                           {},
                           chunk({ConstantOp{ConstantId{0U}}, ConstantOp{ConstantId{1U}},
                                  BinaryOp{BinaryCode::add}, PopOp{}, UnitOp{}, ReturnOp{}}),
                           {}};
    EXPECT_NE(verification_error(bad_add).message.find("not defined"), std::string::npos);
}

TEST(BytecodeVerifierTest, RequiresExactAbstractStateAtControlFlowJoins) {
    BytecodeModule stack_types{
        1U,
        {ConstantValue{true}, ConstantValue{std::int64_t{1}}},
        {},
        chunk({ConstantOp{ConstantId{0U}}, JumpIfFalseOp{InstructionId{4U}}, UnitOp{},
               JumpOp{InstructionId{5U}}, ConstantOp{ConstantId{1U}}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(stack_types).message.find("inconsistent"), std::string::npos);

    BytecodeModule scope_depth{
        1U,
        {ConstantValue{true}},
        {},
        chunk({ConstantOp{ConstantId{0U}}, JumpIfFalseOp{InstructionId{5U}}, EnterScopeOp{},
               UnitOp{}, JumpOp{InstructionId{6U}}, UnitOp{}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(scope_depth).message.find("inconsistent"), std::string::npos);

    BytecodeModule scope_bindings{
        1U,
        {ConstantValue{true}},
        {},
        chunk({EnterScopeOp{}, ConstantOp{ConstantId{0U}}, JumpIfFalseOp{InstructionId{7U}},
               UnitOp{}, DefineLocalOp{"value", false}, UnitOp{}, JumpOp{InstructionId{8U}},
               UnitOp{}, ExitScopeOp{}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(scope_bindings).message.find("inconsistent"), std::string::npos);

    BytecodeModule global_prefix{
        1U,
        {ConstantValue{true}},
        {GlobalBinding{"value", false, test_span}},
        chunk({ConstantOp{ConstantId{0U}}, JumpIfFalseOp{InstructionId{6U}}, UnitOp{},
               InitializeGlobalOp{GlobalId{0U}}, UnitOp{}, JumpOp{InstructionId{7U}}, UnitOp{},
               ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(global_prefix).message.find("inconsistent"), std::string::npos);
}

TEST(BytecodeVerifierTest, EnforcesLexicalBindingRules) {
    const auto scope_underflow = module_with(chunk({ExitScopeOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(scope_underflow).message.find("base scope"), std::string::npos);

    const auto definition_at_root =
        module_with(chunk({UnitOp{}, DefineLocalOp{"value", false}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(definition_at_root).message.find("entered lexical scope"),
              std::string::npos);

    const auto duplicate =
        module_with(chunk({EnterScopeOp{}, UnitOp{}, DefineLocalOp{"value", false}, UnitOp{},
                           DefineLocalOp{"value", true}, UnitOp{}, ExitScopeOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(duplicate).message.find("already defined"), std::string::npos);

    const auto unknown = module_with(chunk({LoadOp{"missing"}, PopOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(unknown).message.find("unknown bytecode binding"),
              std::string::npos);

    const auto immutable =
        module_with(chunk({EnterScopeOp{}, UnitOp{}, DefineLocalOp{"value", false}, UnitOp{},
                           AssignOp{"value"}, PopOp{}, UnitOp{}, ExitScopeOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(immutable).message.find("immutable"), std::string::npos);

    BytecodeModule wrong_type{
        1U,
        {ConstantValue{std::int64_t{1}}, ConstantValue{false}},
        {},
        chunk({EnterScopeOp{}, ConstantOp{ConstantId{0U}}, DefineLocalOp{"value", true},
               ConstantOp{ConstantId{1U}}, AssignOp{"value"}, PopOp{}, UnitOp{}, ExitScopeOp{},
               ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(wrong_type).message.find("expects Int, found Bool"),
              std::string::npos);
}

TEST(BytecodeVerifierTest, EnforcesGlobalInitializationOrderAndSafety) {
    BytecodeModule out_of_order{
        1U,
        {},
        {GlobalBinding{"first", false, test_span}, GlobalBinding{"second", false, test_span}},
        chunk({UnitOp{}, InitializeGlobalOp{GlobalId{1U}}, UnitOp{}, ReturnOp{}}),
        {}};
    EXPECT_NE(verification_error(out_of_order).message.find("source order"), std::string::npos);

    BytecodeModule nested{1U,
                          {},
                          {GlobalBinding{"value", false, test_span}},
                          chunk({EnterScopeOp{}, UnitOp{}, InitializeGlobalOp{GlobalId{0U}},
                                 ExitScopeOp{}, UnitOp{}, ReturnOp{}}),
                          {}};
    EXPECT_NE(verification_error(nested).message.find("main scope"), std::string::npos);

    BytecodeModule missing{
        1U, {}, {GlobalBinding{"value", false, test_span}}, chunk({UnitOp{}, ReturnOp{}}), {}};
    EXPECT_NE(verification_error(missing).message.find("before all globals"), std::string::npos);

    BytecodeModule unsafe_alias{
        1U,
        {},
        {GlobalBinding{"alias", false, test_span}, GlobalBinding{"result", false, test_span}},
        chunk({LoadOp{"read"}, InitializeGlobalOp{GlobalId{0U}}, LoadOp{"alias"}, CallOp{0U},
               InitializeGlobalOp{GlobalId{1U}}, UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"read",
                          {},
                          Type::unit_type(),
                          test_span,
                          chunk({EnterScopeOp{}, UnitOp{}, ExitScopeOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(unsafe_alias).message.find("before all globals"),
              std::string::npos);

    BytecodeModule safe_builtin{1U,
                                {},
                                {GlobalBinding{"ready", false, test_span}},
                                chunk({LoadOp{"frameout"}, UnitOp{}, CallOp{1U},
                                       InitializeGlobalOp{GlobalId{0U}}, UnitOp{}, ReturnOp{}}),
                                {}};
    EXPECT_TRUE(
        std::holds_alternative<VerificationSummary>(BytecodeVerifier{}.verify(safe_builtin)));
}

TEST(BytecodeVerifierTest, ValidatesCallableValuesAritiesAndArguments) {
    const auto non_callable =
        module_with(chunk({UnitOp{}, CallOp{0U}, PopOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(non_callable).message.find("not callable"), std::string::npos);

    const auto wrong_builtin_arity = module_with(
        chunk({LoadOp{"framein"}, UnitOp{}, CallOp{1U}, PopOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(wrong_builtin_arity).message.find("expects 0 arguments"),
              std::string::npos);

    BytecodeModule wrong_argument{
        1U,
        {ConstantValue{false}},
        {},
        chunk({LoadOp{"identity"}, ConstantOp{ConstantId{0U}}, CallOp{1U}, PopOp{}, UnitOp{},
               ReturnOp{}}),
        {CompiledFunction{"identity",
                          {CompiledParameter{"value", Type::int_type(), test_span}},
                          Type::int_type(),
                          test_span,
                          chunk({EnterScopeOp{}, LoadOp{"value"}, ExitScopeOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(wrong_argument).message.find("expects Int, found Bool"),
              std::string::npos);
}

TEST(BytecodeVerifierTest, AllowsFunctionReturnToUnwindTempsAndScopes) {
    BytecodeModule module{
        1U,
        {ConstantValue{std::int64_t{1}}, ConstantValue{std::int64_t{2}}},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"early",
                          {},
                          Type::int_type(),
                          test_span,
                          chunk({EnterScopeOp{}, ConstantOp{ConstantId{0U}}, EnterScopeOp{},
                                 ConstantOp{ConstantId{1U}}, ReturnOp{}})}}};

    const auto summary = verification_summary(module);
    EXPECT_EQ(summary.maximum_stack_depth, 2U);
    EXPECT_EQ(summary.maximum_scope_depth, 3U);
}

TEST(BytecodeVerifierTest, EnforcesMainAndFunctionReturnContracts) {
    const auto extra_main_value = module_with(chunk({UnitOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(extra_main_value).message.find("exactly one Unit"),
              std::string::npos);

    BytecodeModule non_unit_main{1U,
                                 {ConstantValue{std::int64_t{1}}},
                                 {},
                                 chunk({ConstantOp{ConstantId{0U}}, ReturnOp{}}),
                                 {}};
    EXPECT_NE(verification_error(non_unit_main).message.find("exactly one Unit"),
              std::string::npos);

    const auto open_main_scope = module_with(chunk({EnterScopeOp{}, UnitOp{}, ReturnOp{}}));
    EXPECT_NE(verification_error(open_main_scope).message.find("active lexical scopes"),
              std::string::npos);

    BytecodeModule wrong_function_return{
        1U,
        {},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{
            "wrong", {}, Type::int_type(), test_span, chunk({UnitOp{}, ReturnOp{}})}}};
    EXPECT_NE(verification_error(wrong_function_return).message.find("expected Int"),
              std::string::npos);
}

TEST(BytecodeVerifierTest, RejectsUnreachableCodeAndReachableFallthrough) {
    const auto unreachable = module_with(chunk({UnitOp{}, ReturnOp{}, UnitOp{}}));
    EXPECT_NE(verification_error(unreachable).message.find("unreachable instruction"),
              std::string::npos);

    BytecodeModule fallthrough{
        1U,
        {},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"falls", {}, Type::unit_type(), test_span, chunk({UnitOp{}})}}};
    EXPECT_NE(verification_error(fallthrough).message.find("falls off the end"), std::string::npos);
}

TEST(BytecodeVerifierTest, EnforcesResourceLimitsAndCanBeReused) {
    std::vector<CompiledParameter> maximum_parameters;
    maximum_parameters.reserve(max_bytecode_stack_depth + 1U);
    for (std::size_t index = 0; index < max_bytecode_stack_depth; ++index) {
        maximum_parameters.push_back(
            CompiledParameter{"parameter" + std::to_string(index), Type::unit_type(), test_span});
    }
    BytecodeModule parameter_boundary{
        1U,
        {},
        {},
        chunk({UnitOp{}, ReturnOp{}}),
        {CompiledFunction{"boundary", std::move(maximum_parameters), Type::unit_type(), test_span,
                          chunk({UnitOp{}, ReturnOp{}})}}};
    EXPECT_TRUE(
        std::holds_alternative<VerificationSummary>(BytecodeVerifier{}.verify(parameter_boundary)));

    parameter_boundary.functions.front().parameters.push_back(
        CompiledParameter{"excess", Type::unit_type(), test_span});
    EXPECT_NE(verification_error(parameter_boundary).message.find("too many parameters"),
              std::string::npos);

    Chunk too_large;
    too_large.instructions.reserve(max_chunk_instructions + 1U);
    for (std::size_t index = 0; index <= max_chunk_instructions; ++index) {
        too_large.instructions.push_back(instruction(UnitOp{}));
    }
    EXPECT_NE(verification_error(module_with(std::move(too_large))).message.find("limit exceeded"),
              std::string::npos);

    Chunk stack_overflow;
    stack_overflow.instructions.reserve(max_bytecode_stack_depth + 2U);
    for (std::size_t index = 0; index <= max_bytecode_stack_depth; ++index) {
        stack_overflow.instructions.push_back(instruction(UnitOp{}));
    }
    stack_overflow.instructions.push_back(instruction(ReturnOp{}));
    EXPECT_NE(
        verification_error(module_with(std::move(stack_overflow))).message.find("stack depth"),
        std::string::npos);

    const BytecodeVerifier verifier;
    const auto invalid = module_with(chunk({PopOp{}, UnitOp{}, ReturnOp{}}));
    const auto valid = module_with(chunk({UnitOp{}, ReturnOp{}}));
    EXPECT_TRUE(std::holds_alternative<Diagnostic>(verifier.verify(invalid)));
    EXPECT_TRUE(std::holds_alternative<VerificationSummary>(verifier.verify(valid)));
    EXPECT_TRUE(std::holds_alternative<Diagnostic>(verifier.verify(invalid)));
}

} // namespace
} // namespace framestepp
