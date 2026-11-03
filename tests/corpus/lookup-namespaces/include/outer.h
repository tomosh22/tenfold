#pragma once
// Nested namespaces, `namespace a::b::c`, and the same short names (value, Node, get, size) in several scopes.
namespace outer {
int /*@outer_value*/value();
namespace inner {
int /*@inner_value*/value();
struct /*@inner_Node*/Node {
  int /*@inner_Node_get*/get() const;
  int x = 0;
};
} // namespace inner
struct /*@outer_Node*/Node {
  int /*@outer_Node_get*/get() const;
};
} // namespace outer

namespace deep::er::est {
struct /*@est_Thing*/Thing {
  int /*@est_Thing_size*/size() const;
};
int /*@est_make*/make();
} // namespace deep::er::est

// A global with the same short name as the namespaced ones.
int /*@global_value*/value(int scale);
