#include "a/node.h"
#include "a/widget.h"
#include "b/node.h"
#include "b/widget.h"
#include "c/graph.h"
#include "common.h"

int collisions() {
  a::Node an;
  b::Node bn;
  a::Widget aw;
  b::Widget bw;
  int x = an./*@use_a_Node_get*/get() + bn./*@use_b_Node_get*/get();
  x += a::/*@use_a_get*/get(&an) + b::/*@use_b_get*/get(&bn) + a::/*@use_a_get_tpl*/get<1>(an);
  x += a::/*@use_a_size*/size(an) + an./*@use_a_Node_size*/size() + bn./*@use_b_Node_size*/size();
  x += aw./*@use_a_Widget_get*/get() + bw./*@use_b_Widget_get*/get() + aw./*@use_a_Widget_size*/size() + bw./*@use_b_Widget_size*/size();
  a::/*@use_a_Node_type*/Node* np = aw./*@use_a_Widget_node*/node();
  a::/*@use_a_Id*/Id ai = 1;
  b::/*@use_b_Id*/Id bi = 2;
  b::Widget::/*@use_b_Impl_nested*/Impl* bimpl = nullptr;
  (void)bimpl; (void)np;
  struct /*@use_Stat_struct*/Stat st{1};
  int r = /*@use_Stat_func*/Stat("x");
  int g = /*@use_g_get*/get(3) + /*@use_g_size*/size("abc");
  /*@use_g_log*/log("hi");
  log_ns::/*@use_log_ns_log*/log(2);
  c::Edge e{nullptr, nullptr};
  return x + (int)ai + (int)bi + st.v + r + g + (e.from != e.to) + c::/*@use_c_get*/get(e);
}
