#ifndef DATA_HPP
#define DATA_HPP

#include <array>
#include <cstdint>
#include <utility>
#include <variant>

// Namespace el cual contiene todo lo realcionado
// al manejo de información dentro de la base de datos
namespace Db {

// Utilidades
namespace {

// Construir un Variant seleccionando
// el tipo en tiempo de ejecución
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

// String statico pequeño que
// usamos al esciribr campos en el disco
using SmallString = std::array<char, 16>;

// Tipos que acepta la base de datos
enum class Type : std::size_t {
  Int,
  Float,
  Bool,
  String,
};

// Columna de una tabla, tal y como
// se guarda en el disco

// Clase principal para tratar con los campos de
// los registros dentro de un WHERE
struct Value {
  using Variant =
      std::variant<std::int64_t, double, bool, std::array<char, 64>>;
  template <Type type>
  using fromType =
      std::variant_alternative_t<std::to_underlying(type), Variant>;

  // Constructor con tipo en tiempo de ejecución
  Value(Type type) :
      variant{runtimeVariant<Variant>(std::to_underlying(type))} {}
  Value(auto&& _value) : variant{_value} {}

  // Visitor personalizado para mantener encapsulación
  template <class Visitor, class... Values>
  friend decltype(auto) visit(Visitor&& v, Values&&... values) {
    return std::visit(std::forward<Visitor>(v),
                      std::forward<Values>(values).variant...);
  }

  // Get personalizado para mantener encapsulación
  // Solo permite acceder a traves del tipo (y no índice) del campo
  template <Type type>
  fromType<type> get() {
    return std::get<std::to_underlying(type)>(variant);
  }

private:
  Variant variant;
};

// Espacio que ocupa cada tipo en el disco
template <Db::Type Type = Db::Type::Int>
constexpr std::size_t size_of_type(Db::Type type) {
  if (type == Type)
    return sizeof(Db::Value::fromType<Type>);
  else if constexpr (Type != Db::Type::String) {
    constexpr auto Next = static_cast<Db::Type>(std::to_underlying(Type) + 1);
    return size_of_type<Next>(type);
  } else
    return 0;
}

// Visitar un campo en el disco proporcionando información del tipo
template <class Visitor, Db::Type Type = Db::Type::Int>
auto visit_type(const void* pointer, Db::Type type, Visitor&& v) {
  if (type == Type) {
    using Pointer = const Db::Value::fromType<Type>*;
    return v(*reinterpret_cast<Pointer>(pointer));
  } else if constexpr (Type != Db::Type::String) {
    constexpr auto Next = static_cast<Db::Type>(std::to_underlying(Type) + 1);
    return visit_type<Visitor, Next>(pointer, type, std::forward<Visitor>(v));
  } else
    throw std::bad_variant_access();
}

struct Column {
  SmallString name;
  Type type;
};

} // namespace Db

#endif
