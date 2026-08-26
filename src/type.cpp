#include "framestepp/type.hpp"

#include <memory>
#include <utility>

namespace framestepp {

Type::Type(const TypeKind kind) noexcept : kind_{kind} {}

Type::Type(const TypeKind kind, std::unique_ptr<FunctionType> function) noexcept
    : kind_{kind}, function_{std::move(function)} {}

Type::~Type() = default;

Type::Type(const Type& other)
    : kind_{other.kind_},
      function_{other.function_ != nullptr ? std::make_unique<FunctionType>(*other.function_)
                                           : nullptr} {}

Type& Type::operator=(const Type& other) {
    if (this == &other) {
        return *this;
    }
    auto function =
        other.function_ != nullptr ? std::make_unique<FunctionType>(*other.function_) : nullptr;
    kind_ = other.kind_;
    function_ = std::move(function);
    return *this;
}

Type::Type(Type&&) noexcept = default;

Type& Type::operator=(Type&&) noexcept = default;

Type Type::int_type() noexcept { return Type{TypeKind::int_}; }

Type Type::bool_type() noexcept { return Type{TypeKind::bool_}; }

Type Type::string_type() noexcept { return Type{TypeKind::string}; }

Type Type::unit_type() noexcept { return Type{TypeKind::unit}; }

Type Type::function(FunctionType signature) {
    return Type{TypeKind::function, std::make_unique<FunctionType>(std::move(signature))};
}

Type Type::builtin_output() noexcept { return Type{TypeKind::builtin_output}; }

Type Type::builtin_input() noexcept { return Type{TypeKind::builtin_input}; }

Type Type::never() noexcept { return Type{TypeKind::never}; }

TypeKind Type::kind() const noexcept { return kind_; }

const FunctionType* Type::function_type() const noexcept { return function_.get(); }

bool operator==(const Type& left, const Type& right) noexcept {
    if (left.kind_ != right.kind_) {
        return false;
    }
    if (left.kind_ != TypeKind::function) {
        return true;
    }
    if (left.function_ == nullptr || right.function_ == nullptr) {
        return left.function_ == right.function_;
    }
    return *left.function_ == *right.function_;
}

std::string format_type(const Type& type) {
    switch (type.kind()) {
    case TypeKind::int_:
        return "Int";
    case TypeKind::bool_:
        return "Bool";
    case TypeKind::string:
        return "String";
    case TypeKind::unit:
        return "Unit";
    case TypeKind::builtin_output:
        return "builtin output";
    case TypeKind::builtin_input:
        return "builtin input";
    case TypeKind::never:
        return "Never";
    case TypeKind::function: {
        const FunctionType* function = type.function_type();
        if (function == nullptr) {
            return "fn(?)";
        }
        std::string output{"fn("};
        for (std::size_t index = 0; index < function->parameters.size(); ++index) {
            if (index != 0U) {
                output += ", ";
            }
            output += format_type(function->parameters[index]);
        }
        output += ") -> ";
        output += format_type(function->result);
        return output;
    }
    }
    return "unknown type";
}

} // namespace framestepp
