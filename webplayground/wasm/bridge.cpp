#include "framestepp/bytecode.hpp"
#include "framestepp/compiler.hpp"
#include "framestepp/diagnostic.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"
#include "framestepp/source.hpp"
#include "framestepp/type_checker.hpp"
#include "framestepp/verifier.hpp"
#include "framestepp/vm.hpp"

#include <cstddef>
#include <emscripten/bind.h>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t max_playground_source_bytes = 256 * 1024;

struct PlaygroundResult final {
    bool success{false};
    std::string output;
    std::string diagnostic;
    std::size_t executed_instructions{0};
};

using ParseResult = std::variant<framestepp::Program, framestepp::Diagnostic>;

[[nodiscard]] PlaygroundResult unexpected_failure() {
    return PlaygroundResult{
        false,
        {},
        "error: the FrameStep++ compiler stopped unexpectedly",
        0,
    };
}

[[nodiscard]] PlaygroundResult source_too_large() {
    return PlaygroundResult{
        false,
        {},
        "error: playground source cannot exceed 256 KiB",
        0,
    };
}

[[nodiscard]] PlaygroundResult diagnostic_result(const framestepp::SourceFile& source,
                                                 const framestepp::Diagnostic& diagnostic,
                                                 std::string output = {}) {
    return PlaygroundResult{
        false,
        std::move(output),
        framestepp::render_diagnostic(source, diagnostic),
        0,
    };
}

[[nodiscard]] ParseResult parse_source(const framestepp::SourceFile& source) {
    auto lex_result = framestepp::Lexer{source}.lex();
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&lex_result)) {
        return *diagnostic;
    }

    auto tokens = std::move(std::get<std::vector<framestepp::Token>>(lex_result));
    return framestepp::Parser{std::move(tokens)}.parse();
}

[[nodiscard]] framestepp::CompileResult compile_source(const framestepp::SourceFile& source) {
    auto parse_result = parse_source(source);
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&parse_result)) {
        return *diagnostic;
    }

    return framestepp::Compiler{}.compile(std::get<framestepp::Program>(parse_result));
}

[[nodiscard]] PlaygroundResult check(const std::string& text) {
    try {
        if (text.size() > max_playground_source_bytes) {
            return source_too_large();
        }

        const framestepp::SourceFile source{"playground.frame", text};
        auto parse_result = parse_source(source);
        if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&parse_result)) {
            return diagnostic_result(source, *diagnostic);
        }

        const auto& program = std::get<framestepp::Program>(parse_result);
        if (auto diagnostic = framestepp::TypeChecker{}.check(program)) {
            return diagnostic_result(source, *diagnostic);
        }

        return PlaygroundResult{true, "type check passed\n", {}, 0};
    } catch (const std::exception&) {
        return unexpected_failure();
    } catch (...) {
        return unexpected_failure();
    }
}

[[nodiscard]] PlaygroundResult disassemble(const std::string& text) {
    try {
        if (text.size() > max_playground_source_bytes) {
            return source_too_large();
        }

        const framestepp::SourceFile source{"playground.frame", text};
        auto compile_result = compile_source(source);
        if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&compile_result)) {
            return diagnostic_result(source, *diagnostic);
        }

        auto module = std::move(std::get<framestepp::BytecodeModule>(compile_result));
        auto verification = framestepp::BytecodeVerifier{}.verify(module);
        if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&verification)) {
            return diagnostic_result(source, *diagnostic);
        }

        const auto instruction_count =
            std::get<framestepp::VerificationSummary>(verification).instruction_count;
        return PlaygroundResult{
            true,
            framestepp::format_bytecode(module),
            {},
            instruction_count,
        };
    } catch (const std::exception&) {
        return unexpected_failure();
    } catch (...) {
        return unexpected_failure();
    }
}

[[nodiscard]] PlaygroundResult run(const std::string& text, const std::string& input) {
    try {
        if (text.size() > max_playground_source_bytes) {
            return source_too_large();
        }

        const framestepp::SourceFile source{"playground.frame", text};
        auto compile_result = compile_source(source);
        if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&compile_result)) {
            return diagnostic_result(source, *diagnostic);
        }

        auto module = std::move(std::get<framestepp::BytecodeModule>(compile_result));
        auto verification = framestepp::BytecodeVerifier{}.verify(module);
        if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&verification)) {
            return diagnostic_result(source, *diagnostic);
        }

        std::istringstream input_stream{input};
        auto vm_result = framestepp::Vm{}.run(module, input_stream);
        if (const auto* success = std::get_if<framestepp::VmSuccess>(&vm_result)) {
            return PlaygroundResult{
                true,
                success->output,
                {},
                success->executed_instruction_count,
            };
        }

        const auto& failure = std::get<framestepp::VmFailure>(vm_result);
        return diagnostic_result(source, failure.diagnostic, failure.output);
    } catch (const std::exception&) {
        return unexpected_failure();
    } catch (...) {
        return unexpected_failure();
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(framestepp_playground) {
    emscripten::value_object<PlaygroundResult>("PlaygroundResult")
        .field("success", &PlaygroundResult::success)
        .field("output", &PlaygroundResult::output)
        .field("diagnostic", &PlaygroundResult::diagnostic)
        .field("executedInstructions", &PlaygroundResult::executed_instructions);

    emscripten::function("check", &check);
    emscripten::function("run", &run);
    emscripten::function("disassemble", &disassemble);
}
