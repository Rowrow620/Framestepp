#include "framestepp/bytecode.hpp"
#include "framestepp/compiler.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"
#include "framestepp/value.hpp"
#include "framestepp/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>
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

[[nodiscard]] BytecodeModule compile_ok(const std::string_view source) {
    auto program = parse_ok(source);
    auto compiled = Compiler{}.compile(program);
    if (const auto* diagnostic = std::get_if<Diagnostic>(&compiled)) {
        ADD_FAILURE() << "expected compilation to succeed, got: " << diagnostic->message;
        return BytecodeModule{};
    }
    return std::move(std::get<BytecodeModule>(compiled));
}

[[nodiscard]] VmResult run_source(const std::string_view source, const std::string_view input = {},
                                  const Vm& vm = Vm{}) {
    auto module = compile_ok(source);
    std::istringstream input_stream{std::string{input}};
    return vm.run(module, input_stream);
}

[[nodiscard]] VmSuccess run_ok(const std::string_view source, const std::string_view input = {},
                               const Vm& vm = Vm{}) {
    auto result = run_source(source, input, vm);
    if (const auto* failure = std::get_if<VmFailure>(&result)) {
        ADD_FAILURE() << "expected execution to succeed, got: " << failure->diagnostic.message;
        return VmSuccess{};
    }
    return std::move(std::get<VmSuccess>(result));
}

[[nodiscard]] VmFailure run_error(const std::string_view source, const std::string_view input = {},
                                  const Vm& vm = Vm{}) {
    auto result = run_source(source, input, vm);
    if (const auto* success = std::get_if<VmSuccess>(&result)) {
        ADD_FAILURE() << "expected execution to fail, got output: " << success->output;
        return VmFailure{};
    }
    return std::move(std::get<VmFailure>(result));
}

[[nodiscard]] Instruction instruction(Operation operation) {
    return Instruction{std::move(operation), Span{0U, 1U}};
}

[[nodiscard]] BytecodeModule module_with_main(std::vector<ConstantValue> constants,
                                              std::vector<Instruction> instructions) {
    return BytecodeModule{1U, std::move(constants), {}, Chunk{std::move(instructions)}, {}};
}

TEST(VmTest, ExecutesValuesScopesBranchesMutationAndOperators) {
    constexpr std::string_view source = R"(
        fn summarize(start: Int, choose_high: Bool) -> String {
            let mut score = start;
            { let score = 99; frameout(score); };
            score = if choose_high { score + 3 } else { score - 3 };
            frameout(-score);
            frameout(!false);
            if score >= 7 { "go" } else { "wait" }
        }

        let mut label = "";
        label = summarize(2 * 2, true) + "!";
        frameout(label);
        frameout(9 / 2);
        frameout(9 % 2);
        frameout(8 - 3);
        frameout(1 < 2);
        frameout(2 <= 2);
        frameout(3 > 2);
        frameout(3 >= 3);
        frameout(1 == 1);
        frameout(1 != 2);
        frameout({});
    )";

    const auto result = run_ok(source);

    EXPECT_EQ(result.value, Value{UnitValue{}});
    EXPECT_EQ(result.output,
              "99\n-7\ntrue\ngo!\n4\n1\n5\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n()\n");
    EXPECT_GT(result.executed_instruction_count, 0U);
    EXPECT_GT(result.maximum_stack_depth, 0U);
    EXPECT_EQ(result.maximum_user_call_depth, 1U);
}

TEST(VmTest, SupportsForwardMutualRecursionAliasesAndLexicalGlobals) {
    constexpr std::string_view source = R"(
        fn caller() -> Int { let value = 99; read() }
        fn even(value: Int) -> Bool {
            if value == 0 { true } else { odd(value - 1) }
        }
        fn odd(value: Int) -> Bool {
            if value == 0 { false } else { even(value - 1) }
        }
        fn read() -> Int { value }

        let value = 7;
        let predicate = even;
        frameout(predicate(8));
        frameout(caller());
    )";

    const auto result = run_ok(source);

    EXPECT_EQ(result.output, "true\n7\n");
    EXPECT_EQ(result.maximum_user_call_depth, 9U);
}

TEST(VmTest, EvaluatesCalleeArgumentsAndBinaryOperandsLeftToRight) {
    constexpr std::string_view source = R"(
        let mut counter = 0;
        fn next() -> Int { counter = counter + 1; counter }
        fn digits(a: Int, b: Int, c: Int) -> Int { a * 100 + b * 10 + c }
        frameout(next() - next());
        frameout(digits(next(), next(), next()));
        frameout(counter);
    )";

    EXPECT_EQ(run_ok(source).output, "-1\n345\n5\n");
}

TEST(VmTest, EarlyReturnUnwindsNestedScopesAndPendingOperands) {
    constexpr std::string_view source = R"(
        let global = 9;
        fn choose(flag: Bool) -> Int {
            1000 + {
                if flag { return global; };
                global + 1
            }
        }
        frameout(choose(true));
        frameout(choose(false));
        frameout(global);
    )";

    EXPECT_EQ(run_ok(source).output, "9\n1010\n9\n");
}

TEST(VmTest, BuiltinAliasesSurviveShadowingAndDispatchInputAndOutput) {
    constexpr std::string_view source = R"(
        let output = print;
        let input = framein;
        let print = 41;
        fn emit() { output(print); frameout(input()); }
        emit();
    )";

    EXPECT_EQ(run_ok(source, "saved\n").output, "41\nsaved\n");
}

TEST(VmTest, FrameinAcceptsLfCrLfEmptyUnicodeAndFinalUnterminatedLines) {
    constexpr std::string_view source = R"(
        frameout(framein());
        frameout(framein());
        frameout(framein());
        frameout(framein());
        frameout(framein());
    )";
    const std::string input = "first\nsecond\r\n\n\xE9\x9B\xAA\nlast";

    EXPECT_EQ(run_ok(source, input).output, "first\nsecond\n\n\xE9\x9B\xAA\nlast\n");
}

TEST(VmTest, FrameinReportsEofAndInvalidUtf8AtTheCall) {
    constexpr std::string_view source = "frameout(framein());";
    const auto call_start = source.find("framein()");

    const auto eof = run_error(source);
    EXPECT_EQ(eof.diagnostic.message, "framein reached end of input");
    EXPECT_EQ(eof.diagnostic.span, (Span{call_start, call_start + 9U}));
    EXPECT_TRUE(eof.output.empty());

    const std::string invalid_utf8{"\xF0\x28\x8C\x28\n", 5U};
    const auto invalid = run_error(source, invalid_utf8);
    EXPECT_EQ(invalid.diagnostic.message, "framein read invalid UTF-8");
    EXPECT_EQ(invalid.diagnostic.span, (Span{call_start, call_start + 9U}));
    EXPECT_TRUE(invalid.output.empty());
}

TEST(VmTest, FrameinReturnsResultsWhenTheInputStreamEnablesExceptions) {
    const auto module = compile_ok("frameout(framein());");

    std::istringstream empty_input;
    empty_input.exceptions(std::ios::badbit | std::ios::failbit);
    const auto empty_result = Vm{}.run(module, empty_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(empty_result));
    EXPECT_EQ(std::get<VmFailure>(empty_result).diagnostic.message, "framein reached end of input");

    std::istringstream final_line{"last"};
    final_line.exceptions(std::ios::badbit | std::ios::failbit);
    const auto final_result = Vm{}.run(module, final_line);
    ASSERT_TRUE(std::holds_alternative<VmSuccess>(final_result));
    EXPECT_EQ(std::get<VmSuccess>(final_result).output, "last\n");

    std::istringstream bad_input;
    bad_input.setstate(std::ios::badbit);
    const auto bad_result = Vm{}.run(module, bad_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(bad_result));
    EXPECT_EQ(std::get<VmFailure>(bad_result).diagnostic.message,
              "could not read from standard input");
}

TEST(VmTest, FrameinEnforcesLineBoundaryWithoutRejectingTheExactLimit) {
    constexpr std::string_view source = "let line = framein();";
    std::string exact(max_input_line_bytes, 'a');
    exact += "\r\n";
    EXPECT_TRUE(std::holds_alternative<VmSuccess>(run_source(source, exact)));

    std::string oversized(max_input_line_bytes + 1U, 'a');
    oversized.push_back('\n');
    const auto failure = run_error(source, oversized);
    EXPECT_EQ(failure.diagnostic.message, "maximum input line size of 65536 bytes exceeded");
    const auto call_start = source.find("framein()");
    EXPECT_EQ(failure.diagnostic.span, (Span{call_start, call_start + 9U}));
}

TEST(VmTest, FrameinCountsRawBytesAgainstTheAggregateBoundary) {
    std::string source;
    std::string exact_input;
    const std::string line(max_input_line_bytes - 2U, 'x');
    for (std::size_t index = 0; index < 16U; ++index) {
        source += "let line" + std::to_string(index) + " = framein();\n";
        exact_input += line;
        exact_input += "\r\n";
    }
    ASSERT_EQ(exact_input.size(), max_total_input_bytes);
    EXPECT_TRUE(std::holds_alternative<VmSuccess>(run_source(source, exact_input)));

    source += "let overflow = framein();";
    const auto overflow_call = source.rfind("framein()");
    std::string oversized_input = exact_input;
    oversized_input.push_back('x');
    const auto failure = run_error(source, oversized_input);
    EXPECT_EQ(failure.diagnostic.message, "maximum input size of 1048576 bytes exceeded");
    EXPECT_EQ(failure.diagnostic.span, (Span{overflow_call, overflow_call + 9U}));
}

TEST(VmTest, ReportsEveryCheckedArithmeticFailureAtItsOperator) {
    const std::string minimum = "(-9223372036854775807 - 1)";
    const std::string valid_boundaries = "frameout(9223372036854775807 + 0);"
                                         "frameout(" +
                                         minimum +
                                         " - 0);"
                                         "frameout(3037000499 * 3037000499);"
                                         "frameout(" +
                                         minimum +
                                         " * 1);"
                                         "frameout(1 * " +
                                         minimum +
                                         ");"
                                         "frameout(" +
                                         minimum +
                                         " * 0);"
                                         "frameout(0 * " +
                                         minimum +
                                         ");"
                                         "frameout(" +
                                         minimum +
                                         " / 1);"
                                         "frameout(" +
                                         minimum +
                                         " % 1);"
                                         "frameout(-9223372036854775807);";
    EXPECT_EQ(run_ok(valid_boundaries).output,
              "9223372036854775807\n-9223372036854775808\n9223372030926249001\n"
              "-9223372036854775808\n-9223372036854775808\n0\n0\n"
              "-9223372036854775808\n0\n-9223372036854775807\n");

    struct Case final {
        std::string source;
        std::string message;
        std::size_t operator_start;
    };

    std::vector<Case> cases;
    cases.push_back(
        {"frameout(-" + minimum + ");", "integer overflow while evaluating unary `-`", 9U});
    cases.push_back(
        {"frameout(9223372036854775807 + 1);", "integer overflow while evaluating `+`", 29U});
    cases.push_back(
        {"frameout(" + minimum + " - 1);", "integer overflow while evaluating `-`", 0U});
    cases.back().operator_start = cases.back().source.rfind(" - ") + 1U;
    cases.push_back(
        {"frameout(4611686018427387904 * 2);", "integer overflow while evaluating `*`", 29U});
    cases.push_back(
        {"frameout(" + minimum + " * -1);", "integer overflow while evaluating `*`", 0U});
    cases.back().operator_start = cases.back().source.rfind(" * ") + 1U;
    cases.push_back(
        {"frameout(-1 * " + minimum + ");", "integer overflow while evaluating `*`", 0U});
    cases.back().operator_start = cases.back().source.find(" * ") + 1U;
    cases.push_back(
        {"frameout(" + minimum + " * 2);", "integer overflow while evaluating `*`", 0U});
    cases.back().operator_start = cases.back().source.rfind(" * ") + 1U;
    cases.push_back(
        {"frameout(2 * " + minimum + ");", "integer overflow while evaluating `*`", 0U});
    cases.back().operator_start = cases.back().source.find(" * ") + 1U;
    cases.push_back(
        {"frameout(" + minimum + " / -1);", "integer overflow while evaluating `/`", 0U});
    cases.back().operator_start = cases.back().source.rfind(" / ") + 1U;
    cases.push_back(
        {"frameout(" + minimum + " % -1);", "integer overflow while evaluating `%`", 0U});
    cases.back().operator_start = cases.back().source.rfind(" % ") + 1U;
    cases.push_back(
        {"frameout(1 / 0);", "division by zero", std::string{"frameout(1 / 0);"}.find('/')});
    cases.push_back(
        {"frameout(1 % 0);", "remainder by zero", std::string{"frameout(1 % 0);"}.find('%')});

    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.source);
        const auto failure = run_error(test_case.source);
        EXPECT_EQ(failure.diagnostic.message, test_case.message);
        EXPECT_EQ(failure.diagnostic.span,
                  (Span{test_case.operator_start, test_case.operator_start + 1U}));
        EXPECT_TRUE(failure.output.empty());
    }
}

TEST(VmTest, EnforcesStringAndBufferedOutputLimitsAtTheResponsibleInstruction) {
    auto exact_string_module = module_with_main(
        {std::string(max_runtime_string_bytes - 1U, 'a'), std::string{"b"}},
        {instruction(ConstantOp{ConstantId{0U}}), instruction(ConstantOp{ConstantId{1U}}),
         instruction(BinaryOp{BinaryCode::add}), instruction(PopOp{}), instruction(UnitOp{}),
         instruction(ReturnOp{})});
    std::istringstream empty_input;
    EXPECT_TRUE(std::holds_alternative<VmSuccess>(Vm{}.run(exact_string_module, empty_input)));

    auto oversized_string_module = exact_string_module;
    oversized_string_module.constants[0] = std::string(max_runtime_string_bytes, 'a');
    std::istringstream second_empty_input;
    const auto string_result = Vm{}.run(oversized_string_module, second_empty_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(string_result));
    EXPECT_EQ(std::get<VmFailure>(string_result).diagnostic.message,
              "maximum string size of 1048576 bytes exceeded");

    auto output_module = module_with_main(
        {std::string(max_buffered_output_bytes, 'x'), std::string{"y"}},
        {instruction(LoadOp{"frameout"}), instruction(ConstantOp{ConstantId{0U}}),
         instruction(CallOp{1U}), instruction(PopOp{}), instruction(LoadOp{"frameout"}),
         instruction(ConstantOp{ConstantId{1U}}), instruction(CallOp{1U}), instruction(PopOp{}),
         instruction(UnitOp{}), instruction(ReturnOp{})});
    std::istringstream third_empty_input;
    const auto output_result = Vm{}.run(output_module, third_empty_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(output_result));
    const auto& output_failure = std::get<VmFailure>(output_result);
    EXPECT_EQ(output_failure.diagnostic.message,
              "maximum buffered output size of 1048576 bytes exceeded");
    EXPECT_EQ(output_failure.output.size(), max_buffered_output_bytes + 1U);
    EXPECT_EQ(output_failure.output.back(), '\n');
}

TEST(VmTest, RuntimeFailurePreservesPriorOutputAndStopsAtCallDepthLimit) {
    constexpr std::string_view arithmetic_source = "frameout(\"before\"); frameout(1 / 0);";
    const auto arithmetic = run_error(arithmetic_source);
    EXPECT_EQ(arithmetic.output, "before\n");
    EXPECT_EQ(arithmetic.diagnostic.message, "division by zero");

    constexpr std::string_view recursion_source = R"(
        fn forever() { forever(); }
        frameout("before");
        forever();
    )";
    const auto recursion = run_error(recursion_source);
    EXPECT_EQ(recursion.output, "before\n");
    EXPECT_EQ(recursion.diagnostic.message, "maximum function call depth exceeded");
    const auto recursive_call = recursion_source.find("forever();");
    EXPECT_EQ(recursion.diagnostic.span,
              (Span{recursive_call, recursive_call + std::string_view{"forever()"}.size()}));
}

TEST(VmTest, EnforcesExecutionStepOperandStackAndScopeLimits) {
    BytecodeModule step_module;
    step_module.source_size = 1U;
    std::vector<Instruction> function_instructions;
    function_instructions.reserve(15'607U);
    function_instructions.push_back(instruction(EnterScopeOp{}));
    for (std::size_t index = 0; index < 7'800U; ++index) {
        function_instructions.push_back(instruction(UnitOp{}));
        function_instructions.push_back(instruction(PopOp{}));
    }
    function_instructions.push_back(instruction(LoadOp{"spin"}));
    function_instructions.push_back(instruction(CallOp{0U}));
    function_instructions.push_back(instruction(PopOp{}));
    function_instructions.push_back(instruction(UnitOp{}));
    function_instructions.push_back(instruction(ExitScopeOp{}));
    function_instructions.push_back(instruction(ReturnOp{}));
    step_module.functions.push_back(CompiledFunction{
        "spin", {}, Type::unit_type(), Span{0U, 1U}, Chunk{std::move(function_instructions)}});
    step_module.main.instructions = {instruction(LoadOp{"spin"}), instruction(CallOp{0U}),
                                     instruction(PopOp{}), instruction(UnitOp{}),
                                     instruction(ReturnOp{})};

    std::istringstream empty_input;
    const auto step_result = Vm{}.run(step_module, empty_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(step_result));
    EXPECT_EQ(std::get<VmFailure>(step_result).diagnostic.message,
              "maximum execution step count of 1000000 exceeded");

    constexpr std::size_t pending_values_per_call = 34U;
    BytecodeModule stack_module;
    stack_module.source_size = 1U;
    stack_module.constants.push_back(std::int64_t{1});
    std::vector<Instruction> stack_function;
    stack_function.reserve((pending_values_per_call * 2U) + 7U);
    stack_function.push_back(instruction(EnterScopeOp{}));
    for (std::size_t index = 0; index < pending_values_per_call; ++index) {
        stack_function.push_back(instruction(ConstantOp{ConstantId{0U}}));
    }
    stack_function.push_back(instruction(LoadOp{"grow"}));
    stack_function.push_back(instruction(CallOp{0U}));
    for (std::size_t index = 0; index < pending_values_per_call + 1U; ++index) {
        stack_function.push_back(instruction(PopOp{}));
    }
    stack_function.push_back(instruction(UnitOp{}));
    stack_function.push_back(instruction(ExitScopeOp{}));
    stack_function.push_back(instruction(ReturnOp{}));
    stack_module.functions.push_back(CompiledFunction{
        "grow", {}, Type::unit_type(), Span{0U, 1U}, Chunk{std::move(stack_function)}});
    stack_module.main.instructions = {instruction(LoadOp{"grow"}), instruction(CallOp{0U}),
                                      instruction(PopOp{}), instruction(UnitOp{}),
                                      instruction(ReturnOp{})};
    std::istringstream stack_input;
    const auto stack_result = Vm{}.run(stack_module, stack_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(stack_result));
    EXPECT_EQ(std::get<VmFailure>(stack_result).diagnostic.message,
              "maximum operand stack depth of 4096 exceeded");

    std::vector<Instruction> scope_instructions;
    scope_instructions.reserve((max_runtime_scope_depth * 2U) + 2U);
    for (std::size_t index = 0; index < max_runtime_scope_depth; ++index) {
        scope_instructions.push_back(instruction(EnterScopeOp{}));
    }
    scope_instructions.push_back(instruction(UnitOp{}));
    for (std::size_t index = 0; index < max_runtime_scope_depth; ++index) {
        scope_instructions.push_back(instruction(ExitScopeOp{}));
    }
    scope_instructions.push_back(instruction(ReturnOp{}));
    auto exact_scope_module = module_with_main({}, std::move(scope_instructions));
    std::istringstream scope_input;
    EXPECT_TRUE(std::holds_alternative<VmSuccess>(Vm{}.run(exact_scope_module, scope_input)));

    auto oversized_scope_module = exact_scope_module;
    oversized_scope_module.main.instructions.insert(
        oversized_scope_module.main.instructions.begin(), instruction(EnterScopeOp{}));
    std::istringstream oversized_scope_input;
    const auto scope_result = Vm{}.run(oversized_scope_module, oversized_scope_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(scope_result));
    EXPECT_NE(std::get<VmFailure>(scope_result).diagnostic.message.find("scope depth"),
              std::string::npos);
}

TEST(VmTest, RejectsMalformedModulesBeforeExecutingTheirPrefix) {
    auto module = module_with_main(
        {std::string{"must not print"}},
        {instruction(LoadOp{"frameout"}), instruction(ConstantOp{ConstantId{0U}}),
         instruction(CallOp{1U}), instruction(PopOp{}), instruction(ConstantOp{ConstantId{99U}}),
         instruction(PopOp{}), instruction(UnitOp{}), instruction(ReturnOp{})});

    std::istringstream empty_input;
    const auto result = Vm{}.run(module, empty_input);

    ASSERT_TRUE(std::holds_alternative<VmFailure>(result));
    const auto& failure = std::get<VmFailure>(result);
    EXPECT_TRUE(failure.output.empty());
    EXPECT_NE(failure.diagnostic.message.find("constant index 99"), std::string::npos);

    const std::string invalid_utf8{"\xFF", 1U};
    auto invalid_string_module = module_with_main(
        {std::string{"must not print"}, invalid_utf8},
        {instruction(LoadOp{"frameout"}), instruction(ConstantOp{ConstantId{0U}}),
         instruction(CallOp{1U}), instruction(PopOp{}), instruction(ConstantOp{ConstantId{1U}}),
         instruction(PopOp{}), instruction(UnitOp{}), instruction(ReturnOp{})});

    std::istringstream invalid_string_input;
    const auto invalid_string_result = Vm{}.run(invalid_string_module, invalid_string_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(invalid_string_result));
    const auto& invalid_string_failure = std::get<VmFailure>(invalid_string_result);
    EXPECT_TRUE(invalid_string_failure.output.empty());
    EXPECT_EQ(invalid_string_failure.diagnostic.message, "string constant is not valid UTF-8");

    auto unused_invalid_string_module =
        module_with_main({invalid_utf8}, {instruction(UnitOp{}), instruction(ReturnOp{})});
    std::istringstream unused_invalid_string_input;
    const auto unused_invalid_string_result =
        Vm{}.run(unused_invalid_string_module, unused_invalid_string_input);
    ASSERT_TRUE(std::holds_alternative<VmFailure>(unused_invalid_string_result));
    EXPECT_TRUE(std::get<VmFailure>(unused_invalid_string_result).output.empty());
}

TEST(VmTest, ReusingVmStartsFreshAfterSuccessAndFailure) {
    const Vm vm;
    EXPECT_EQ(run_ok("let score = 1; frameout(score);", {}, vm).output, "1\n");

    const auto failure = run_error("frameout(\"before\"); frameout(1 / 0);", {}, vm);
    EXPECT_EQ(failure.output, "before\n");

    const auto final = run_ok("let score = 2; frameout(score);", {}, vm);
    EXPECT_EQ(final.output, "2\n");
    EXPECT_EQ(final.value, Value{UnitValue{}});
}

} // namespace
} // namespace framestepp
