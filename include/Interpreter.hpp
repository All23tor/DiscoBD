#ifndef INTERPRETER_HPP
#define INTERPRETER_HPP

#include "Type.hpp"

#include <cstring>
#include <functional>
#include <memory>

// Patrón de diseño Interpreter para parsear las expresiones WHERE
namespace Db {

// Clase nodo abstracta que representa un valos dentro del árbol sintáctico
struct Node {
  virtual ~Node() = default;
  // Evaluación requiere de un contexto (un registro e informaciónde sus tipos)
  virtual Value evaluate(const char*, const Column*) const = 0;
};

namespace {
using NodePtr = std::unique_ptr<Node>;

// Clase que representa un valor constante
struct ValueNode final : public Node {
  const Value number;

  ValueNode(const Value& _number) : number{_number} {}
  ValueNode(Value&& _number) : number{std::move(_number)} {}
  ~ValueNode() override = default;
  // Retorna el valor almacenado (no usa el contexto)
  Value evaluate(const char*, const Column*) const override {
    return number;
  }
};

// Clase que representa una variable (campo de un registro)
struct Variable final : public Node {
  const std::size_t index;

  Variable(std::size_t _name) : index{_name} {}
  ~Variable() override = default;
  // Solicita el valor contenido en el registro
  Value evaluate(const char* record, const Column* schema) const override {
    return visit_field(record, index, schema, [](auto&& arg) -> Value {
      return arg;
    });
  }
};

// Template de clase que representa una operación entre dos valores
// Utilizamos un Functor para evaluar la operación
template <class Func>
struct Operation final : public Node {
  // Composición del functor que acepta cualquier combinación de tipos
  // (necesario para usar visit)
  static constexpr auto Visitor = [](auto&& a, auto&& b) -> Value {
    if constexpr (requires {
                    Func{}(std::string(a.data()), std::string(b.data()));
                  }) {
      auto res = Func{}(std::string(a.data()), std::string(b.data()));
      if constexpr (std::is_same_v<decltype(res), std::string>) {
        Value::fromType<Type::String> conv_res;
        std::strcpy(conv_res.data(), res.c_str());
        return conv_res;
      } else
        return res;
    } else if constexpr (requires { Func{}(a, b); })
      return Func{}(a, b);
    else
      throw std::invalid_argument("Syntax error: Invalid operands");
  };
  const NodePtr left;
  const NodePtr right;

  Operation(NodePtr&& _left, NodePtr&& _right) :
      left(std::move(_left)),
      right(std::move(_right)) {}
  virtual ~Operation() override = default;
  // Evalúa el nodo llamando al funtor con doas argumentos
  Value evaluate(const char* record, const Column* context) const override {
    auto lhs = left->evaluate(record, context);
    auto rhs = right->evaluate(record, context);
    return visit(Visitor, std::move(lhs), std::move(rhs));
  }
};

// Función auxiliar para crear nodos de operaciones
using NodeFactory = NodePtr (*)(NodePtr&&, NodePtr&&);
template <typename T>
constexpr NodeFactory op_factory =
    [](NodePtr&& left, NodePtr&& right) -> NodePtr {
  return std::make_unique<Operation<T>>(std::move(left), std::move(right));
};

struct OperationInfo {
  std::string_view name;
  NodeFactory factory;
};

// Tabla de operaciones soportadas (y su representación textual)
constexpr std::array<OperationInfo, 13> operations{{
    {"||", op_factory<std::logical_or<>>},
    {"&&", op_factory<std::logical_and<>>},
    {">=", op_factory<std::greater_equal<>>},
    {"<=", op_factory<std::less_equal<>>},
    {">", op_factory<std::greater<>>},
    {"<", op_factory<std::less<>>},
    {"==", op_factory<std::equal_to<>>},
    {"!=", op_factory<std::not_equal_to<>>},
    {"+", op_factory<std::plus<>>},
    {"-", op_factory<std::minus<>>},
    {"*", op_factory<std::multiplies<>>},
    {"/", op_factory<std::divides<>>},
    {"%", op_factory<std::modulus<>>},
}};

std::size_t handle_unary_minus(std::string_view expr, std::size_t pos) {
  if (pos == 0)
    pos = expr.find("-", 1);
  while (pos != std::string_view::npos && !std::isalnum(expr[pos - 1]))
    pos = expr.find("-", pos + 1);
  return pos;
}

std::pair<std::size_t, OperationInfo> find_lowest(std::string_view expr) {
  for (const auto& operation : operations) {
    auto pos = expr.find(operation.name);
    if (operation.name == "-")
      pos = handle_unary_minus(expr, pos);

    if (pos == std::string_view::npos)
      continue;

    int parenCount = 0;
    for (char c : expr.substr(0, pos))
      if (c == ')')
        parenCount--;
      else if (c == '(')
        parenCount++;

    if (parenCount != 0)
      continue;

    return {pos, operation};
  }
  return {std::string_view::npos, {}};
}

bool balanced_parenthesis(std::string& expression) {
  int depth = 0;
  for (std::size_t i = 0; i < expression.length() - 1; ++i) {
    char c = expression[i];
    if (c == '(')
      ++depth;
    else if (c == ')')
      --depth;
    if (depth == 0)
      return false;
  }
  return true;
}

Value::Variant parse_as_value(std::string&& expression) {
  if (expression == "true")
    return true;
  if (expression == "false")
    return false;
  if (expression.contains('.'))
    return std::stod(expression);
  if (expression.front() == '"') {
    Value::fromType<Type::String> arr;
    for (int i = 0; i < expression.length() - 2; i++)
      arr[i] = expression[i + 1];
    arr[expression.length() - 2] = '\0';
    return arr;
  }
  return std::stol(expression);
}

NodePtr makeTree(std::string&& expression, const Column* columns,
                 std::size_t column_size) {
  while (expression.front() == '(' && expression.back() == ')')
    if (balanced_parenthesis(expression))
      expression = expression.substr(1, expression.length() - 2);
    else
      break;

  auto [pos, op] = find_lowest(expression);
  if (pos == std::string_view::npos) {
    for (auto idx = 0uz; idx < column_size; idx++) {
      auto name = &columns[idx].name[0];
      if (std::strcmp(name, expression.c_str()) == 0)
        return std::make_unique<Variable>(idx);
    }
    return std::make_unique<ValueNode>(parse_as_value(std::move(expression)));
  }

  auto leftNode = makeTree(expression.substr(0, pos), columns, column_size);
  auto rightNode =
      makeTree(expression.substr(pos + op.name.size()), columns, column_size);
  return op.factory(std::move(leftNode), std::move(rightNode));
}
} // namespace

inline std::unique_ptr<Node> parseExpression(std::string expression,
                                             const Column* columns,
                                             std::size_t column_size) {
  std::erase(expression, ' ');
  auto tree = makeTree(std::move(expression), columns, column_size);
  return tree;
}
} // namespace Db

#endif
