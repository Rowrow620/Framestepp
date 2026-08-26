#pragma once

#include <memory>
#include <string>
#include <vector>

namespace framestepp {

struct FunctionType;

enum class TypeKind {
    int_,
    bool_,
    string,
    unit,
    function,
    builtin_output,
    builtin_input,
    never,
};

/// A copyable static type with deeply owned function-signature data.
class Type final {
  public:
    ~Type();

    Type(const Type& other);
    Type& operator=(const Type& other);
    Type(Type&&) noexcept;
    Type& operator=(Type&&) noexcept;

    [[nodiscard]] static Type int_type() noexcept;
    [[nodiscard]] static Type bool_type() noexcept;
    [[nodiscard]] static Type string_type() noexcept;
    [[nodiscard]] static Type unit_type() noexcept;
    [[nodiscard]] static Type function(FunctionType signature);
    [[nodiscard]] static Type builtin_output() noexcept;
    [[nodiscard]] static Type builtin_input() noexcept;
    [[nodiscard]] static Type never() noexcept;

    [[nodiscard]] TypeKind kind() const noexcept;
    [[nodiscard]] const FunctionType* function_type() const noexcept;

    friend bool operator==(const Type& left, const Type& right) noexcept;

  private:
    explicit Type(TypeKind kind) noexcept;
    Type(TypeKind kind, std::unique_ptr<FunctionType> function) noexcept;

    TypeKind kind_;
    std::unique_ptr<FunctionType> function_;
};

struct FunctionType final {
    std::vector<Type> parameters;
    Type result;

    friend bool operator==(const FunctionType&, const FunctionType&) noexcept = default;
};

/// Produces the stable source-facing spelling used in type diagnostics.
[[nodiscard]] std::string format_type(const Type& type);

} // namespace framestepp
