#ifndef INTERPRETER_HPP
#define INTERPRETER_HPP

#include "Type.hpp"
#include <memory>

namespace Db {

struct Node {
  virtual ~Node() = default;
  virtual Value evaluate(const char*, const Column*) const = 0;
};
using NodePtr = std::unique_ptr<Node>;
NodePtr parseExpression(std::string_view, const Column*, std::size_t);
} // namespace Db

#endif
