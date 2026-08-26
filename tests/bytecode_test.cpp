#include "framestepp/bytecode.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace framestepp {
namespace {

static_assert(!std::is_convertible_v<std::uint32_t, ConstantId>);
static_assert(!std::is_convertible_v<ConstantId, FunctionId>);
static_assert(std::variant_size_v<Operation> == 15U);

[[nodiscard]] Instruction instruction(Operation operation, const Span span = Span{10U, 11U}) {
    return Instruction{std::move(operation), span};
}

TEST(BytecodeTest, OwnsAndDeepCopiesItsData) {
    BytecodeModule original{
        100U,
        {std::int64_t{42}, true, std::string{"Ada"}},
        {GlobalBinding{"answer", true, Span{1U, 7U}}},
        Chunk{{instruction(ConstantOp{ConstantId{0U}}), instruction(ReturnOp{})}},
        {CompiledFunction{"identity",
                          {CompiledParameter{"value", Type::int_type(), Span{20U, 25U}}},
                          Type::function(FunctionType{{Type::int_type()}, Type::string_type()}),
                          Span{12U, 40U},
                          Chunk{{instruction(LoadOp{"value"}), instruction(ReturnOp{})}}}},
    };

    BytecodeModule copy = original;

    EXPECT_EQ(copy, original);
    ASSERT_NE(copy.functions[0].result_type.function_type(), nullptr);
    EXPECT_NE(copy.functions[0].result_type.function_type(),
              original.functions[0].result_type.function_type());
    std::get<std::string>(copy.constants[2]) = "Grace";
    EXPECT_NE(copy, original);
    EXPECT_EQ(std::get<std::string>(original.constants[2]), "Ada");
}

TEST(BytecodeFormatTest, ProducesTheExactDeterministicLayout) {
    const BytecodeModule module{
        64U,
        {std::int64_t{42}, false, std::string{"go\n"}},
        {GlobalBinding{"message", true, Span{5U, 12U}}},
        Chunk{{instruction(ConstantOp{ConstantId{2U}}, Span{13U, 19U}),
               instruction(ReturnOp{}, Span{20U, 21U})}},
        {CompiledFunction{
            "choose",
            {CompiledParameter{"flag", Type::bool_type(), Span{29U, 33U}}},
            Type::string_type(),
            Span{22U, 50U},
            Chunk{{instruction(LoadOp{"flag"}, Span{30U, 34U}),
                   instruction(ReturnOp{}, Span{35U, 36U})}},
        }},
    };

    constexpr std::string_view expected =
        "== constants ==\n"
        "0000 Integer 42\n"
        "0001 Boolean false\n"
        "0002 String \"go\\n\"\n"
        "== globals ==\n"
        "0000 mut message @ [5, 12)\n"
        "== main ==\n"
        "0000 CONSTANT 0002            @ [13, 19)\n"
        "0001 RETURN                   @ [20, 21)\n"
        "== function 0000 choose(flag: Bool) -> String @ [22, 50) ==\n"
        "0000 LOAD flag                @ [30, 34)\n"
        "0001 RETURN                   @ [35, 36)\n";

    EXPECT_EQ(format_bytecode(module), expected);
    EXPECT_EQ(format_bytecode(module), format_bytecode(module));
}

TEST(BytecodeFormatTest, FormatsEveryOperationWithStableSpellings) {
    Chunk main;
    main.instructions = {
        instruction(ConstantOp{ConstantId{12'345U}}),
        instruction(UnitOp{}),
        instruction(LoadOp{"value"}),
        instruction(InitializeGlobalOp{GlobalId{7U}}),
        instruction(DefineLocalOp{"fixed", false}),
        instruction(DefineLocalOp{"changeable", true}),
        instruction(AssignOp{"changeable"}),
        instruction(EnterScopeOp{}),
        instruction(ExitScopeOp{}),
        instruction(PopOp{}),
        instruction(UnaryOp{UnaryCode::negate}),
        instruction(UnaryOp{UnaryCode::not_}),
        instruction(BinaryOp{BinaryCode::add}),
        instruction(BinaryOp{BinaryCode::subtract}),
        instruction(BinaryOp{BinaryCode::multiply}),
        instruction(BinaryOp{BinaryCode::divide}),
        instruction(BinaryOp{BinaryCode::remainder}),
        instruction(BinaryOp{BinaryCode::equal}),
        instruction(BinaryOp{BinaryCode::not_equal}),
        instruction(BinaryOp{BinaryCode::less}),
        instruction(BinaryOp{BinaryCode::less_equal}),
        instruction(BinaryOp{BinaryCode::greater}),
        instruction(BinaryOp{BinaryCode::greater_equal}),
        instruction(JumpIfFalseOp{InstructionId{30U}}),
        instruction(JumpOp{InstructionId{31U}}),
        instruction(CallOp{2U}),
        instruction(ReturnOp{}),
    };
    const BytecodeModule module{32U, {}, {}, std::move(main), {}};

    const auto output = format_bytecode(module);
    for (const std::string_view spelling : {
             "CONSTANT 12345",
             "UNIT",
             "LOAD value",
             "INITIALIZE_GLOBAL 0007",
             "DEFINE_LOCAL let fixed",
             "DEFINE_LOCAL mut changeable",
             "ASSIGN changeable",
             "ENTER_SCOPE",
             "EXIT_SCOPE",
             "POP",
             "NEGATE",
             "NOT",
             "ADD",
             "SUBTRACT",
             "MULTIPLY",
             "DIVIDE",
             "REMAINDER",
             "EQUAL",
             "NOT_EQUAL",
             "LESS",
             "LESS_EQUAL",
             "GREATER",
             "GREATER_EQUAL",
             "JUMP_IF_FALSE 0030",
             "JUMP 0031",
             "CALL 2",
             "RETURN",
         }) {
        EXPECT_NE(output.find(spelling), std::string::npos) << spelling;
    }
}

TEST(BytecodeFormatTest, EscapesStringsAndMalformedNamesWithoutLocaleDependence) {
    std::string constant{"\\\"\n\r\t"};
    constant.push_back('\x01');
    constant.push_back('\x7F');
    constant += "\xC3\xA9";
    const BytecodeModule module{
        8U,
        {std::move(constant)},
        {GlobalBinding{"bad\nname", false, Span{7U, 2U}}},
        Chunk{{instruction(LoadOp{""}), instruction(AssignOp{"has space"})}},
        {},
    };

    const auto output = format_bytecode(module);
    std::string escaped_constant{R"(0000 String "\\\"\n\r\t\x01\x7F)"};
    escaped_constant += "\xC3\xA9";
    escaped_constant.push_back('"');
    EXPECT_NE(output.find(escaped_constant), std::string::npos);
    EXPECT_NE(output.find(R"(0000 let "bad\nname" @ [7, 2))"), std::string::npos);
    EXPECT_NE(output.find("LOAD \"\""), std::string::npos);
    EXPECT_NE(output.find(R"(ASSIGN "has space")"), std::string::npos);
}

TEST(BytecodeFormatTest, RemainsInspectableWhenOperandsAreMalformed) {
    const BytecodeModule module{
        0U,
        {},
        {},
        Chunk{{instruction(ConstantOp{ConstantId{999U}}, Span{9U, 1U}),
               instruction(UnaryOp{static_cast<UnaryCode>(99)}),
               instruction(BinaryOp{static_cast<BinaryCode>(99)}),
               instruction(JumpOp{InstructionId{999U}})}},
        {},
    };

    const auto output = format_bytecode(module);
    EXPECT_NE(output.find("CONSTANT 0999"), std::string::npos);
    EXPECT_NE(output.find("UNKNOWN_UNARY"), std::string::npos);
    EXPECT_NE(output.find("UNKNOWN_BINARY"), std::string::npos);
    EXPECT_NE(output.find("JUMP 0999"), std::string::npos);
    EXPECT_NE(output.find("@ [9, 1)"), std::string::npos);
    EXPECT_EQ(output.back(), '\n');
}

} // namespace
} // namespace framestepp
