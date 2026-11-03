#include <string>
#include "tb/stack.h"
#include "tb/types.h"

// A second translation unit that instantiates the same templates with other arguments.
int second() {
  tb::Stack<std::string> names;
  names./*@use2_push*/push("a");
  names./*@use2_top*/top() += "b";
  tb::Pair<std::string, int> pr{"k", 1};
  return static_cast<int>(names.size()) + pr.second;
}
