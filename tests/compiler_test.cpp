#include "framestepp/bytecode.hpp"
#include "framestepp/compiler.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

[[nodiscard]] Program parse_ok(const std::string_view source) {
    auto lexed = Lexer{source}.lex();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&lexed)) {
        ADD_FAILURE() << "expected lexing to succeed, got: " << diagnostic->message;
        return Program{{}, Span{}};
    }

    auto parsed = Parser{std::move(std::get<std::vector<Token>>(lexed))}.parse();
    if (const auto* diagnostic = std::get_if<Diagnostic>(&parsed)) {
        ADD_FAILURE() << "expected parsing to succeed, got: " << diagnostic->message;
        return Program{{}, Span{}};
    }
    return std::move(std::get<Program>(parsed));
}

[[nodiscard]] BytecodeModule compile_ok(const std::string_view source,
                                        const Compiler& compiler = Compiler{}) {
    auto program = parse_ok(source);
    auto compiled = compiler.compile(program);
    if (const auto* diagnostic = std::get_if<Diagnostic>(&compiled)) {
        ADD_FAILURE() << "expected compilation to succeed, got: " << diagnostic->message;
        return BytecodeModule{};
    }
    return std::move(std::get<BytecodeModule>(compiled));
}

[[nodiscard]] std::vector<Operation> operations(const Chunk& chunk) {
    std::vector<Operation> result;
    result.reserve(chunk.instructions.size());
    for (const auto& instruction : chunk.instructions) {
        result.push_back(instruction.operation);
    }
    return result;
}

TEST(CompilerTest, PreservesSourceOrderForConstantsGlobalsAndFunctions) {
    constexpr std::string_view source = "fn first(value: Int) -> Int { value + 1 }\n"
                                        "let flag = true;\n"
                                        "fn second() -> String { \"two\" }\n"
                                        "let name = \"Ada\";\n"
                                        "frameout(first(2));\n";

    const auto module = compile_ok(source);

    EXPECT_EQ(module.source_size, source.size());
    EXPECT_EQ(module.constants,
              (std::vector<ConstantValue>{std::int64_t{1}, true, std::string{"two"},
                                          std::string{"Ada"}, std::int64_t{2}}));

    ASSERT_EQ(module.globals.size(), 2U);
    EXPECT_EQ(module.globals[0].name, "flag");
    EXPECT_FALSE(module.globals[0].mutable_binding);
    EXPECT_EQ(module.globals[1].name, "name");
    EXPECT_FALSE(module.globals[1].mutable_binding);

    ASSERT_EQ(module.functions.size(), 2U);
    EXPECT_EQ(module.functions[0].name, "first");
    ASSERT_EQ(module.functions[0].parameters.size(), 1U);
    EXPECT_EQ(module.functions[0].parameters[0].name, "value");
    EXPECT_EQ(module.functions[0].parameters[0].type, Type::int_type());
    EXPECT_EQ(module.functions[0].result_type, Type::int_type());
    EXPECT_EQ(module.functions[1].name, "second");
    EXPECT_TRUE(module.functions[1].parameters.empty());
    EXPECT_EQ(module.functions[1].result_type, Type::string_type());

    EXPECT_EQ(operations(module.main), (std::vector<Operation>{
                                           ConstantOp{ConstantId{1}},
                                           InitializeGlobalOp{GlobalId{0}},
                                           ConstantOp{ConstantId{3}},
                                           InitializeGlobalOp{GlobalId{1}},
                                           LoadOp{"frameout"},
                                           LoadOp{"first"},
                                           ConstantOp{ConstantId{4}},
                                           CallOp{1},
                                           CallOp{1},
                                           PopOp{},
                                           UnitOp{},
                                           ReturnOp{},
                                       }));
}

TEST(CompilerTest, PatchesIfExpressionsWithAbsoluteForwardJumps) {
    const auto module = compile_ok("fn choose(flag: Bool) -> Int { if flag { 1 } else { 2 } }");

    ASSERT_EQ(module.functions.size(), 1U);
    const auto& instructions = module.functions[0].chunk.instructions;

    std::size_t conditional_offset = instructions.size();
    std::size_t end_jump_offset = instructions.size();
    for (std::size_t offset = 0; offset < instructions.size(); ++offset) {
        if (std::holds_alternative<JumpIfFalseOp>(instructions[offset].operation)) {
            conditional_offset = offset;
        } else if (std::holds_alternative<JumpOp>(instructions[offset].operation)) {
            end_jump_offset = offset;
        }
    }

    ASSERT_LT(conditional_offset, instructions.size());
    ASSERT_LT(end_jump_offset, instructions.size());
    const auto false_target =
        std::get<JumpIfFalseOp>(instructions[conditional_offset].operation).target.value;
    const auto end_target = std::get<JumpOp>(instructions[end_jump_offset].operation).target.value;

    EXPECT_GT(false_target, end_jump_offset);
    EXPECT_GT(end_target, false_target);
    ASSERT_LT(static_cast<std::size_t>(false_target), instructions.size());
    ASSERT_LT(static_cast<std::size_t>(end_target), instructions.size());
    EXPECT_TRUE(std::holds_alternative<EnterScopeOp>(instructions[false_target].operation));
    EXPECT_TRUE(std::holds_alternative<ExitScopeOp>(instructions[end_target].operation));
}

TEST(CompilerTest, KeepsAssignmentsAsValuesForEnclosingCalls) {
    const auto module = compile_ok("let mut score = 0; frameout(score = 7);");

    ASSERT_EQ(module.globals.size(), 1U);
    EXPECT_EQ(module.globals[0].name, "score");
    EXPECT_TRUE(module.globals[0].mutable_binding);
    EXPECT_EQ(operations(module.main), (std::vector<Operation>{
                                           ConstantOp{ConstantId{0}},
                                           InitializeGlobalOp{GlobalId{0}},
                                           LoadOp{"frameout"},
                                           ConstantOp{ConstantId{1}},
                                           AssignOp{"score"},
                                           CallOp{1},
                                           PopOp{},
                                           UnitOp{},
                                           ReturnOp{},
                                       }));
}

TEST(CompilerTest, PreservesBlockTailsAndSynthesizesUnit) {
    const auto valued = compile_ok("let answer = { 42 };");
    EXPECT_EQ(operations(valued.main), (std::vector<Operation>{
                                           EnterScopeOp{},
                                           ConstantOp{ConstantId{0}},
                                           ExitScopeOp{},
                                           InitializeGlobalOp{GlobalId{0}},
                                           UnitOp{},
                                           ReturnOp{},
                                       }));

    const auto unit = compile_ok("let answer = { 42; };");
    EXPECT_EQ(operations(unit.main), (std::vector<Operation>{
                                         EnterScopeOp{},
                                         ConstantOp{ConstantId{0}},
                                         PopOp{},
                                         UnitOp{},
                                         ExitScopeOp{},
                                         InitializeGlobalOp{GlobalId{0}},
                                         UnitOp{},
                                         ReturnOp{},
                                     }));
}

TEST(CompilerTest, MapsOperationsBackToTheirSourceTokens) {
    constexpr std::string_view source = "let mut score = 1; score = -score + 2;";
    const auto module = compile_ok(source);
    const auto& instructions = module.main.instructions;

    for (const auto& instruction : instructions) {
        EXPECT_TRUE(instruction.span.is_valid());
        EXPECT_LE(instruction.span.end, source.size());
    }

    const auto minus = source.find('-');
    const auto plus = source.find('+');
    const auto assignment = source.find('=', source.find(';') + 1U);
    bool found_negate = false;
    bool found_add = false;
    bool found_assignment = false;
    for (const auto& instruction : instructions) {
        if (const auto* unary = std::get_if<UnaryOp>(&instruction.operation);
            unary != nullptr && unary->operation == UnaryCode::negate) {
            found_negate = true;
            EXPECT_EQ(instruction.span, (Span{minus, minus + 1U}));
        }
        if (const auto* binary = std::get_if<BinaryOp>(&instruction.operation);
            binary != nullptr && binary->operation == BinaryCode::add) {
            found_add = true;
            EXPECT_EQ(instruction.span, (Span{plus, plus + 1U}));
        }
        if (std::holds_alternative<AssignOp>(instruction.operation)) {
            found_assignment = true;
            EXPECT_EQ(instruction.span, (Span{assignment, assignment + 1U}));
        }
    }
    EXPECT_TRUE(found_negate);
    EXPECT_TRUE(found_add);
    EXPECT_TRUE(found_assignment);
}

TEST(CompilerTest, CanBeReusedWithoutLeakingModuleState) {
    const Compiler compiler;
    const auto first = compile_ok("fn one() -> Int { 1 } let name = \"Ada\";", compiler);
    const auto second = compile_ok("frameout(9);", compiler);
    const auto fresh = compile_ok("frameout(9);");

    ASSERT_EQ(first.constants.size(), 2U);
    ASSERT_EQ(first.functions.size(), 1U);
    ASSERT_EQ(first.globals.size(), 1U);

    EXPECT_EQ(second, fresh);
    EXPECT_EQ(second.constants, (std::vector<ConstantValue>{std::int64_t{9}}));
    EXPECT_TRUE(second.functions.empty());
    EXPECT_TRUE(second.globals.empty());
}

TEST(CompilerTest, OmitsInstructionsAfterAnEarlyReturn) {
    const auto module = compile_ok("fn stop() -> Int { return 1; frameout(99); 2 }");

    ASSERT_EQ(module.functions.size(), 1U);
    EXPECT_EQ(module.constants, (std::vector<ConstantValue>{std::int64_t{1}}));
    EXPECT_EQ(operations(module.functions[0].chunk),
              (std::vector<Operation>{EnterScopeOp{}, ConstantOp{ConstantId{0}}, ReturnOp{}}));
}

TEST(CompilerTest, EmitsOnlyReachableConditionalJoins) {
    const auto both_return =
        compile_ok("fn choose(flag: Bool) -> Int { if flag { return 1; } else { return 2; } }");
    ASSERT_EQ(both_return.functions.size(), 1U);
    const auto both_operations = operations(both_return.functions[0].chunk);
    EXPECT_EQ(std::count_if(both_operations.begin(), both_operations.end(),
                            [](const Operation& operation) {
                                return std::holds_alternative<ReturnOp>(operation);
                            }),
              2);
    EXPECT_TRUE(std::none_of(
        both_operations.begin(), both_operations.end(),
        [](const Operation& operation) { return std::holds_alternative<JumpOp>(operation); }));

    const auto one_returns =
        compile_ok("fn choose(flag: Bool) -> Int { if flag { return 1; } else { 2 } }");
    ASSERT_EQ(one_returns.functions.size(), 1U);
    const auto one_operations = operations(one_returns.functions[0].chunk);
    EXPECT_TRUE(
        std::none_of(one_operations.begin(), one_operations.end(), [](const Operation& operation) {
            return std::holds_alternative<JumpOp>(operation);
        }));
    EXPECT_EQ(std::count_if(one_operations.begin(), one_operations.end(),
                            [](const Operation& operation) {
                                return std::holds_alternative<ReturnOp>(operation);
                            }),
              2);
}

TEST(CompilerTest, ReturnsTypeErrorsBeforeLowering) {
    constexpr std::string_view source = "let score: Int = \"forty-two\";";
    auto program = parse_ok(source);
    auto result = Compiler{}.compile(program);

    ASSERT_TRUE(std::holds_alternative<Diagnostic>(result));
    const auto& diagnostic = std::get<Diagnostic>(result);
    EXPECT_EQ(diagnostic.message, "variable `score` expects Int, found String");
    EXPECT_EQ(source.substr(diagnostic.span.start, diagnostic.span.size()), "\"forty-two\"");
}

} // namespace
} // namespace framestepp
