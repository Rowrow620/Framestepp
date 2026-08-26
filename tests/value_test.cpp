#include "framestepp/value.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace framestepp {
namespace {

TEST(ValueTest, FormatsEveryRuntimeValueDeterministically) {
    BytecodeModule module;
    module.functions.push_back(CompiledFunction{"choose", {}, Type::int_type(), Span{}, Chunk{}});

    EXPECT_EQ(format_value(Value{std::int64_t{42}}, module), "42");
    EXPECT_EQ(format_value(Value{std::numeric_limits<std::int64_t>::min()}, module),
              "-9223372036854775808");
    EXPECT_EQ(format_value(Value{true}, module), "true");
    EXPECT_EQ(format_value(Value{false}, module), "false");
    EXPECT_EQ(format_value(Value{std::string{"Frame\nStep \xE9\x9B\xAA"}}, module),
              "Frame\nStep \xE9\x9B\xAA");
    EXPECT_EQ(format_value(Value{UnitValue{}}, module), "()");
    EXPECT_EQ(format_value(Value{FunctionValue{FunctionId{0U}}}, module), "<fn choose>");
    EXPECT_EQ(format_value(Value{BuiltinValue{BuiltinCode::frameout}}, module),
              "<builtin frameout>");
    EXPECT_EQ(format_value(Value{BuiltinValue{BuiltinCode::print}}, module), "<builtin print>");
    EXPECT_EQ(format_value(Value{BuiltinValue{BuiltinCode::framein}}, module), "<builtin framein>");
}

TEST(ValueTest, RuntimeStringsOwnTheirBytes) {
    std::string source = "saved";
    Value value{source};
    source.assign("changed");

    EXPECT_EQ(std::get<std::string>(value), "saved");
}

} // namespace
} // namespace framestepp
