#include "c/graph.h"
namespace c {
struct /*@c_Node*/Node {
  int /*@c_Node_get*/get() const { return 3; }
  int /*@c_Node_size*/size() const { return 4; }
};
} // namespace c
namespace {
class /*@c_anon_Impl*/Impl {
 public:
  int /*@c_anon_Impl_get*/get() const { return 5; }
  int /*@c_anon_Impl_size*/size() const { return 6; }
};
} // namespace
static int /*@c_static_get*/get(int k) { return k; }
int c::/*@c_get_def*/get(const Edge& e) {
  /*@use_c_anon_Impl*/Impl impl;
  return e.from->/*@use_c_Node_get*/get() + e.to->/*@use_c_Node_size*/size() + impl./*@use_c_anon_Impl_get*/get() + impl./*@use_c_anon_Impl_size*/size() + ::/*@use_c_static_get*/get(1);
}
