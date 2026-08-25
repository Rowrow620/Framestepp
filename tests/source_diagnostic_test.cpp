#include "framestepp/diagnostic.hpp"
#include "framestepp/source.hpp"

#include <gtest/gtest.h>
#include <string>

namespace framestepp {
namespace {

TEST(SpanTest, UsesHalfOpenByteOffsets) {
    constexpr Span span{3, 7};

    static_assert(span.is_valid());
    static_assert(span.size() == 4);
    static_assert(span.contains(3));
    static_assert(span.contains(6));
    static_assert(!span.contains(7));
    static_assert(Span{5, 5}.empty());
    static_assert(!Span{5, 3}.is_valid());
    static_assert(Span{5, 3}.size() == 0);
}

TEST(SourceFileTest, MapsByteOffsetsAcrossUtf8Lines) {
    const SourceFile source{"sample.frame", "first\ncaf\xC3\xA9\nlast"};

    EXPECT_EQ(source.location(0), (SourceLocation{1, 1, 0}));
    EXPECT_EQ(source.location(6), (SourceLocation{2, 1, 6}));
    EXPECT_EQ(source.location(9), (SourceLocation{2, 4, 9}));
    EXPECT_EQ(source.location(11), (SourceLocation{2, 5, 11}));
    EXPECT_EQ(source.location(12), (SourceLocation{3, 1, 12}));
    EXPECT_FALSE(source.location(10).has_value());
    EXPECT_FALSE(source.location(source.size() + 1).has_value());

    ASSERT_TRUE(source.line_text(2).has_value());
    EXPECT_EQ(*source.line_text(2), "caf\xC3\xA9");
}

TEST(SourceFileTest, ExcludesWindowsLineEndingsFromLineText) {
    const SourceFile source{"sample.frame", "first\r\nsecond"};

    EXPECT_EQ(source.line_span(1), (Span{0, 5}));
    ASSERT_TRUE(source.line_text(1).has_value());
    EXPECT_EQ(*source.line_text(1), "first");
    EXPECT_EQ(source.location(5), (SourceLocation{1, 6, 5}));
    EXPECT_EQ(source.location(6), (SourceLocation{1, 6, 6}));
    EXPECT_EQ(source.location(7), (SourceLocation{2, 1, 7}));
}

TEST(SourceFileTest, TreatsMalformedUtf8BytesAsStandaloneLocations) {
    const SourceFile source{"invalid.frame", std::string{"ok\n\x80\xE2", 5}};

    EXPECT_EQ(source.location(3), (SourceLocation{2, 1, 3}));
    EXPECT_EQ(source.location(4), (SourceLocation{2, 2, 4}));
    EXPECT_EQ(source.location(5), (SourceLocation{2, 3, 5}));
}

TEST(SourceFileTest, TreatsLoneCarriageReturnAsALineEnding) {
    const SourceFile source{"legacy.frame", "first\r"};

    EXPECT_EQ(source.line_span(1), (Span{0, 5}));
    EXPECT_EQ(source.location(5), (SourceLocation{1, 6, 5}));
    EXPECT_EQ(source.location(6), (SourceLocation{2, 1, 6}));
    ASSERT_TRUE(source.line_text(2).has_value());
    EXPECT_TRUE(source.line_text(2)->empty());
}

TEST(DiagnosticTest, RendersLocationSourceAndCaret) {
    const SourceFile source{"test.frame", "let answer = 42;\nlet broken = @;\n"};
    const Diagnostic diagnostic{
        DiagnosticSeverity::error,
        "unexpected character",
        Span{30, 31},
    };

    const auto rendered = render_diagnostic(source, diagnostic);

    EXPECT_EQ(rendered, "error: unexpected character\n"
                        " --> test.frame:2:14\n"
                        "  |\n"
                        "2 | let broken = @;\n"
                        "  |              ^");
}

TEST(DiagnosticTest, KeepsMalformedUtf8DiagnosticOnItsSourceLine) {
    const SourceFile source{"invalid.frame", std::string{"ok\n\x80", 4}};
    const Diagnostic diagnostic{
        DiagnosticSeverity::error,
        "invalid UTF-8",
        Span{3, 4},
    };

    const auto rendered = render_diagnostic(source, diagnostic);

    EXPECT_NE(rendered.find("--> invalid.frame:2:1"), std::string::npos);
    EXPECT_NE(rendered.find("2 | "), std::string::npos);
}

TEST(DiagnosticTest, PlacesCrLfDiagnosticAtVisibleLineEnd) {
    const SourceFile source{"windows.frame", "first\r\nsecond"};
    const Diagnostic diagnostic{
        DiagnosticSeverity::error,
        "unexpected line break",
        Span{6, 7},
    };

    EXPECT_EQ(render_diagnostic(source, diagnostic), "error: unexpected line break\n"
                                                     " --> windows.frame:1:6\n"
                                                     "  |\n"
                                                     "1 | first\n"
                                                     "  |      ^");
}

} // namespace
} // namespace framestepp
