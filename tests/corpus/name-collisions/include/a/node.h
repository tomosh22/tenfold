#pragma once
#include "a/fwd.h"
namespace a {
struct /*@a_Node*/Node {
  int /*@a_Node_get*/get() const;
  int /*@a_Node_size*/size() const;
  /*@use_a_Node_in_Node*/Node* /*@a_Node_next*/next = nullptr;
  /*@use_a_Id_in_Node*/Id /*@a_Node_id*/id = 0;
  friend class /*@a_fr_Impl*/Impl;   // befriends a::Impl, not b::Impl
};
int /*@a_get*/get(const Node*);
int /*@a_size*/size(const Node&);
template <int I>
int /*@a_get_tpl*/get(const Node& n) { return I + n.id; }
} // namespace a
