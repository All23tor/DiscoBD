#ifndef DATA_HPP
#define DATA_HPP

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <utility>
#include <variant>

namespace Db {
namespace {
template <class Variant, std::size_t Idx = 0>
Variant runtimeVariant(std::size_t idx) {
  using Alternative = std::variant_alternative_t<Idx, Variant>;
  if (idx == Idx)
    return Alternative{};
  else if constexpr (Idx + 1 < std::variant_size_v<Variant>)
    return runtimeVariant<Variant, Idx + 1>(idx);
  else
    throw std::bad_variant_access();
}
} // namespace

enum class Type : std::size_t {
  Int,
  Float,
  Bool,
  String,
};

inline std::istream& operator>>(std::istream& is, Db::Type& type) {
  static const std::map<std::string, Db::Type> typeNames = {
      {"INT", Db::Type::Int},
      {"FLOAT", Db::Type::Float},
      {"BOOL", Db::Type::Bool},
      {"STRING", Db::Type::String}};

  std::string name;
  std::getline(is, name, ' ');
  type = typeNames.at(name);
  return is;
}

struct Value {
  using Variant =
      std::variant<std::int64_t, double, bool, std::array<char, 64>>;
  template <Type type>
  using fromType =
      std::variant_alternative_t<std::to_underlying(type), Variant>;

  Value(Type type) :
      variant{runtimeVariant<Variant>(std::to_underlying(type))} {}
  Value(auto&& _value) : variant{_value} {}

  template <class Visitor, class... Values>
  friend decltype(auto) visit(Visitor&& v, Values&&... values) {
    return std::visit(std::forward<Visitor>(v),
                      std::forward<Values>(values).variant...);
  }

  template <Type type>
  fromType<type> get() {
    return std::get<std::to_underlying(type)>(variant);
  }

private:
  Variant variant;
};

template <Db::Type Type = Db::Type::Int>
std::size_t size_of_type(Db::Type type) {
  if (type == Type)
    return sizeof(Db::Value::fromType<Type>);
  else if constexpr (Type != Db::Type::String) {
    constexpr auto Next = static_cast<Db::Type>(std::to_underlying(Type) + 1);
    return size_of_type<Next>(type);
  } else
    return 0;
}
} // namespace Db

#endif
