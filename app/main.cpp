#include "framestepp/ast.hpp"
#include "framestepp/bytecode.hpp"
#include "framestepp/compiler.hpp"
#include "framestepp/diagnostic.hpp"
#include "framestepp/lexer.hpp"
#include "framestepp/parser.hpp"
#include "framestepp/source.hpp"
#include "framestepp/token.hpp"
#include "framestepp/type_checker.hpp"
#include "framestepp/verifier.hpp"
#include "framestepp/vm.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using SourceResult = std::variant<framestepp::SourceFile, std::string>;

void print_help(std::ostream& output) {
    output << "FrameStep++\n\n"
              "Usage:\n"
              "  framestepp lex <FILE>          Print the source token stream\n"
              "  framestepp parse <FILE>        Parse and print the syntax tree\n"
              "  framestepp check <FILE>        Validate a program's types\n"
              "  framestepp disassemble <FILE>  Print verified bytecode\n"
              "  framestepp run <FILE>          Compile and execute on the VM\n"
              "  framestepp --version           Print version information\n"
              "  framestepp --help              Print this help\n";
}

[[nodiscard]] SourceResult read_source(const std::string_view path_text) {
    const std::filesystem::path path{path_text};
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return "could not read `" + path.string() + "`";
    }

    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        return "could not finish reading `" + path.string() + "`";
    }
    return framestepp::SourceFile{path.string(), std::move(text)};
}

int report_diagnostic(const framestepp::SourceFile& source,
                      const framestepp::Diagnostic& diagnostic) {
    std::cerr << framestepp::render_diagnostic(source, diagnostic) << '\n';
    return 1;
}

int lex_file(const framestepp::SourceFile& source) {
    auto result = framestepp::Lexer{source}.lex();
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&result)) {
        return report_diagnostic(source, *diagnostic);
    }

    for (const auto& token : std::get<std::vector<framestepp::Token>>(result)) {
        std::cout << token.span.start << ".." << token.span.end << "  "
                  << framestepp::token_description(token) << '\n';
    }
    return 0;
}

[[nodiscard]] std::optional<framestepp::Program>
parse_source(const framestepp::SourceFile& source) {
    auto lex_result = framestepp::Lexer{source}.lex();
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&lex_result)) {
        report_diagnostic(source, *diagnostic);
        return std::nullopt;
    }

    auto tokens = std::move(std::get<std::vector<framestepp::Token>>(lex_result));
    auto parse_result = framestepp::Parser{std::move(tokens)}.parse();
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&parse_result)) {
        report_diagnostic(source, *diagnostic);
        return std::nullopt;
    }

    return std::move(std::get<framestepp::Program>(parse_result));
}

int parse_file(const framestepp::SourceFile& source) {
    auto program = parse_source(source);
    if (!program.has_value()) {
        return 1;
    }

    std::cout << framestepp::format_ast(*program);
    return 0;
}

int check_file(const framestepp::SourceFile& source) {
    auto program = parse_source(source);
    if (!program.has_value()) {
        return 1;
    }

    if (auto diagnostic = framestepp::TypeChecker{}.check(*program)) {
        return report_diagnostic(source, *diagnostic);
    }

    std::cout << "type check passed\n";
    return 0;
}

int disassemble_file(const framestepp::SourceFile& source) {
    auto program = parse_source(source);
    if (!program.has_value()) {
        return 1;
    }

    auto compile_result = framestepp::Compiler{}.compile(*program);
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&compile_result)) {
        return report_diagnostic(source, *diagnostic);
    }

    auto module = std::move(std::get<framestepp::BytecodeModule>(compile_result));
    auto verification = framestepp::BytecodeVerifier{}.verify(module);
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&verification)) {
        return report_diagnostic(source, *diagnostic);
    }

    std::cout << framestepp::format_bytecode(module);
    return 0;
}

[[nodiscard]] bool write_program_output(const std::string& output) {
    std::cout << output << std::flush;
    if (std::cout) {
        return true;
    }
    std::cerr << "error: could not write program output\n";
    return false;
}

int run_file(const framestepp::SourceFile& source) {
    auto program = parse_source(source);
    if (!program.has_value()) {
        return 1;
    }

    auto compile_result = framestepp::Compiler{}.compile(*program);
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&compile_result)) {
        return report_diagnostic(source, *diagnostic);
    }

    auto module = std::move(std::get<framestepp::BytecodeModule>(compile_result));
    auto verification = framestepp::BytecodeVerifier{}.verify(module);
    if (const auto* diagnostic = std::get_if<framestepp::Diagnostic>(&verification)) {
        return report_diagnostic(source, *diagnostic);
    }

    auto result = framestepp::Vm{}.run(module, std::cin);
    if (const auto* success = std::get_if<framestepp::VmSuccess>(&result)) {
        return write_program_output(success->output) ? 0 : 1;
    }

    const auto& failure = std::get<framestepp::VmFailure>(result);
    if (!write_program_output(failure.output)) {
        return 1;
    }
    return report_diagnostic(source, failure.diagnostic);
}

} // namespace

int main(const int argument_count, char* arguments[]) {
    if (argument_count == 1) {
        print_help(std::cout);
        return 0;
    }

    const std::string_view command{arguments[1]};
    if (argument_count == 2 && (command == "--help" || command == "-h")) {
        print_help(std::cout);
        return 0;
    }
    if (argument_count == 2 && (command == "--version" || command == "-V")) {
        std::cout << "FrameStep++ 0.1.0\n";
        return 0;
    }

    if (argument_count != 3 || (command != "lex" && command != "parse" && command != "check" &&
                                command != "disassemble" && command != "run")) {
        std::cerr << "error: invalid arguments\n\n"
                     "Run `framestepp --help` for usage.\n";
        return 1;
    }

    auto source_result = read_source(arguments[2]);
    if (const auto* message = std::get_if<std::string>(&source_result)) {
        std::cerr << "error: " << *message << '\n';
        return 1;
    }

    const auto& source = std::get<framestepp::SourceFile>(source_result);
    if (command == "lex") {
        return lex_file(source);
    }
    if (command == "parse") {
        return parse_file(source);
    }
    if (command == "check") {
        return check_file(source);
    }
    if (command == "disassemble") {
        return disassemble_file(source);
    }
    return run_file(source);
}
