#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"
#include "framestepp/type.hpp"
#include "framestepp/type_checker.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace framestepp {
namespace {

static_assert(std::is_copy_constructible_v<Type>);
static_assert(std::is_copy_assignable_v<Type>);
static_assert(std::is_nothrow_move_constructible_v<Type>);
static_assert(max_type_check_depth == 128U);

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

[[nodiscard]] TypeCheckResult check(const std::string_view source) {
    auto program = parse_ok(source);
    return TypeChecker{}.check(program);
}

void expect_ok(const std::string_view source) {
    const auto diagnostic = check(source);
    ASSERT_FALSE(diagnostic.has_value()) << diagnostic->message;
}

[[nodiscard]] Diagnostic type_error(const std::string_view source) {
    auto diagnostic = check(source);
    if (!diagnostic) {
        ADD_FAILURE() << "expected type checking to fail";
        return Diagnostic{DiagnosticSeverity::error, "missing diagnostic", Span{}};
    }
    return *diagnostic;
}

[[nodiscard]] std::string_view covered_text(const std::string_view source, const Span span) {
    if (span.end > source.size() || span.start > span.end) {
        ADD_FAILURE() << "diagnostic span is outside the source";
        return {};
    }
    return source.substr(span.start, span.size());
}

TEST(TypeTest, DeepCopiesAndFormatsFunctionSignatures) {
    const Type original =
        Type::function(FunctionType{{Type::int_type(), Type::string_type()}, Type::bool_type()});
    Type copy = original;
    Type assigned = Type::unit_type();
    assigned = copy;

    EXPECT_EQ(format_type(original), "fn(Int, String) -> Bool");
    EXPECT_EQ(copy, original);
    EXPECT_EQ(assigned, original);
    ASSERT_NE(copy.function_type(), nullptr);
    EXPECT_NE(copy.function_type(), original.function_type());
}

TEST(TypeCheckerTest, ChecksTheAdaProgramAndExactPrimitiveTypes) {
    expect_ok(R"(
        fn greet(name: String) -> String { "Hello, " + name }
        let player = "Ada";
        let answer: Int = 42;
        let ready: Bool = true;
        let finished: Unit = {};
        frameout(greet(player));
    )");
}

TEST(TypeCheckerTest, InfersBindingsAndValidatesAnnotations) {
    expect_ok(R"(let number = 42; let text = "FrameStep++"; let flag = false;)");

    constexpr std::string_view source = R"(let answer: Int = "forty-two";)";
    const auto diagnostic = type_error(source);
    EXPECT_EQ(diagnostic.message, "variable `answer` expects Int, found String");
    EXPECT_EQ(covered_text(source, diagnostic.span), R"("forty-two")");
    EXPECT_EQ(type_error("let answer: Number = 42;").message, "unknown type `Number`");
}

TEST(TypeCheckerTest, EnforcesMutabilityAndAssignmentTypes) {
    expect_ok("let mut answer: Int = 41; answer = answer + 1;");
    EXPECT_EQ(type_error("let answer = 42; answer = 43;").message,
              "cannot assign to immutable variable `answer`");

    constexpr std::string_view source = R"(let mut answer = 42; answer = "no";)";
    const auto diagnostic = type_error(source);
    EXPECT_EQ(diagnostic.message, "assignment to `answer` expects Int, found String");
    EXPECT_EQ(covered_text(source, diagnostic.span), "=");
    EXPECT_EQ(type_error("missing = 1;").message, "unknown variable `missing`");
}

TEST(TypeCheckerTest, EnforcesLexicalScopesShadowingAndDuplicateRules) {
    expect_ok("let value = 1; { let value = true; frameout(value); }; frameout(value);");
    EXPECT_EQ(type_error("{ let secret = 42; }; frameout(secret);").message,
              "unknown variable `secret`");
    EXPECT_EQ(type_error("let value = 1; let value = 2;").message,
              "name `value` is already defined in this scope");
    EXPECT_EQ(type_error("fn broken(value: Int, value: Int) { }").message,
              "parameter `value` is already declared in this function");
}

TEST(TypeCheckerTest, ValidatesUnaryAndBinaryOperatorsAtTheirSpans) {
    expect_ok(R"(
        let arithmetic = 2 + 3 * 4 - 5 / 1 % 2;
        let ordering = 1 < 2 == true;
        let text = "Frame" + "Step";
        let equality = text != "other";
        let negative = -1;
        let opposite = !false;
    )");
    EXPECT_EQ(type_error("let value = !1;").message, "unary `!` requires Bool, found Int");

    constexpr std::string_view source = R"(let value = 1 + "two";)";
    const auto diagnostic = type_error(source);
    EXPECT_EQ(diagnostic.message, "operator `+` is not defined for Int and String");
    EXPECT_EQ(covered_text(source, diagnostic.span), "+");
    EXPECT_EQ(type_error("let same = frameout == print;").message,
              "operator `==` is not defined for builtin output and builtin output");
}

TEST(TypeCheckerTest, ValidatesIfConditionsAndJoinsBranches) {
    expect_ok("let value = if true { 1 } else { 2 };");
    expect_ok("if true { frameout(1); };");
    EXPECT_EQ(type_error("if 1 { };").message, "`if` condition must be Bool, found Int");
    EXPECT_EQ(type_error(R"(let value = if true { 1 } else { "two" };)").message,
              "`if` branches have incompatible types: Int and String");
    EXPECT_EQ(type_error("if true { 1 };").message,
              "`if` branches have incompatible types: Int and Unit");
}

TEST(TypeCheckerTest, HoistsFunctionsForForwardCallsAndMutualRecursion) {
    expect_ok(R"(
        fn first(value: Int) -> Int { second(value) }
        fn second(value: Int) -> Int { value + 1 }
        frameout(first(41));
    )");
    expect_ok(R"(
        fn even(n: Int) -> Bool { if n == 0 { true } else { odd(n - 1) } }
        fn odd(n: Int) -> Bool { if n == 0 { false } else { even(n - 1) } }
    )");
    expect_ok(R"(
        fn fibonacci(n: Int) -> Int {
            if n <= 1 { n } else { fibonacci(n - 1) + fibonacci(n - 2) }
        }
    )");
}

TEST(TypeCheckerTest, SupportsExactFunctionAliases) {
    expect_ok(R"(
        fn double(value: Int) -> Int { value * 2 }
        let alias = double;
        frameout(alias(21));
    )");
    expect_ok(R"(
        fn consume(value: Int) { frameout(value); }
        let mut alias = consume;
        alias = consume;
    )");
    EXPECT_EQ(type_error(R"(
        fn consume(value: Int) { frameout(value); }
        let mut output = frameout;
        output = consume;
    )")
                  .message,
              "assignment to `output` expects builtin output, found fn(Int) -> Unit");
}

TEST(TypeCheckerTest, ValidatesCallsArgumentsAndCallableValues) {
    constexpr std::string_view source = R"(
        fn double(value: Int) -> Int { value * 2 }
        double("two");
    )";
    const auto diagnostic = type_error(source);
    EXPECT_EQ(diagnostic.message, "argument 1 expects Int, found String");
    EXPECT_EQ(covered_text(source, diagnostic.span), R"("two")");
    EXPECT_EQ(type_error("fn identity(value: Int) -> Int { value } identity();").message,
              "function expected 1 argument, but received 0");
    EXPECT_EQ(type_error("let value = 42; value();").message, "value of type Int is not callable");
}

TEST(TypeCheckerTest, ValidatesExplicitAndImplicitFunctionReturns) {
    EXPECT_EQ(type_error("fn answer() -> Int { }").message,
              "function `answer` must return Int, found Unit");
    EXPECT_EQ(type_error(R"(fn answer() -> Int { return "no"; })").message,
              "return expects Int, found String");
    expect_ok("fn done() { return; }");
    EXPECT_EQ(type_error("fn answer() { 42 }").message,
              "function `answer` must return Unit, found Int");
    EXPECT_EQ(type_error("fn answer() -> Int { return; }").message,
              "return expects Int, found Unit");
    EXPECT_EQ(type_error("return 1;").message, "`return` can only be used inside a function");
}

TEST(TypeCheckerTest, TreatsReturningExpressionsAsNever) {
    expect_ok("fn choose(flag: Bool) -> Int { if flag { return 1; }; 2 }");
    expect_ok("fn stop() -> Int { return 1; }");
    expect_ok("fn both(flag: Bool) -> Int { if flag { return 1; } else { return 2; } }");
    expect_ok(R"(fn stop() -> Int { return 1; "unreachable" })");
    expect_ok("fn answer() -> Int { let result: Int = { return 42; }; result }");
}

TEST(TypeCheckerTest, StillChecksUnreachableCode) {
    EXPECT_EQ(type_error(R"(fn stop() -> Int { return 1; missing; })").message,
              "unknown variable `missing`");
    EXPECT_EQ(type_error(R"(fn stop() -> Int { return 1; "bad" + 2 })").message,
              "operator `+` is not defined for String and Int");
}

TEST(TypeCheckerTest, SupportsThemedBuiltinsAndCompatibilityAlias) {
    expect_ok(R"(
        let output = frameout;
        let name: String = framein();
        let input = framein;
        let other: String = input();
        frameout(1);
        print(true);
        output("Ada");
        frameout({});
        frameout(frameout);
    )");
    EXPECT_EQ(type_error("frameout();").message, "function expected 1 argument, but received 0");
    EXPECT_EQ(type_error("framein(1);").message, "function expected 0 arguments, but received 1");
    EXPECT_EQ(type_error("let mut input = framein; input = frameout;").message,
              "assignment to `input` expects builtin input, found builtin output");
}

TEST(TypeCheckerTest, AllowsUserGlobalsToShadowEveryPreludeName) {
    expect_ok(R"(
        fn frameout(value: Int) -> Int { value + 1 }
        let print = 42;
        let framein = "saved";
        frameout(print);
    )");
    EXPECT_EQ(type_error("let frameout = 42; frameout(1);").message,
              "value of type Int is not callable");
}

TEST(TypeCheckerTest, MakesCompletedGlobalScopeVisibleToFunctions) {
    expect_ok("fn read() -> Int { answer } let answer = 42; frameout(read());");
    expect_ok("let answer = 42; fn read() -> Int { answer }");
    EXPECT_EQ(type_error("fn announce() { frameout(1); } let frameout = 42;").message,
              "value of type Int is not callable");
}

TEST(TypeCheckerTest, ProtectsGlobalInitializationOrder) {
    EXPECT_EQ(type_error("let answer: Int = read(); fn read() -> Int { answer }").message,
              "global initializers cannot call user-defined functions");
    expect_ok(R"(let initialized = frameout("ready");)");
    expect_ok(R"(let output = print; let initialized = output("ready");)");
    expect_ok("let input = framein; let initialized: String = input();");
    EXPECT_EQ(type_error("print(1); let answer = 42;").message,
              "global variables must be declared before top-level expressions");
    EXPECT_EQ(type_error("let answer = answer;").message, "unknown variable `answer`");
}

TEST(TypeCheckerTest, ReportsArgumentErrorsBeforeGlobalCallSafety) {
    EXPECT_EQ(type_error(R"(
        fn identity(value: Int) -> Int { value }
        let answer = identity(missing);
    )")
                  .message,
              "unknown variable `missing`");
    EXPECT_EQ(type_error(R"(
        fn identity(value: Int) -> Int { value }
        let answer = identity();
    )")
                  .message,
              "global initializers cannot call user-defined functions");
}

TEST(TypeCheckerTest, CapsTypeCheckingExpressionDepth) {
    std::string expression;
    for (std::size_t index = 0; index < 100U; ++index) {
        if (!expression.empty()) {
            expression += " + ";
        }
        expression += "1";
    }
    const std::string source =
        "let value = " + std::string(32U, '(') + expression + std::string(32U, ')') + ";";
    EXPECT_EQ(type_error(source).message, "maximum type-checking expression depth exceeded");
}

TEST(TypeCheckerTest, CanBeReusedAfterFailuresWithoutLeakingState) {
    auto bad_program = parse_ok("fn broken() -> Int { return false; }");
    auto good_program = parse_ok("fn good() -> Int { 42 }");
    const TypeChecker checker;

    ASSERT_TRUE(checker.check(bad_program).has_value());
    EXPECT_FALSE(checker.check(good_program).has_value());
    EXPECT_TRUE(checker.check(bad_program).has_value());
}

TEST(TypeCheckerTest, HandlesManyHoistedFunctionSignatures) {
    std::string source;
    for (std::size_t index = 0; index < 2'000U; ++index) {
        source += "fn function_" + std::to_string(index) + "() {}\n";
    }
    expect_ok(source);
}

} // namespace
} // namespace framestepp
