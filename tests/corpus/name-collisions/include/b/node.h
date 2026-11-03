#pragma once
#include "b/fwd.h"
namespace b {
struct /*@b_Node*/Node {
  int /*@b_Node_get*/get() const;
  int /*@b_Node_size*/size() const;
  /*@use_b_Id_in_Node*/Id /*@b_Node_id*/id = 0;
  friend struct /*@b_fr_Impl*/Impl;   // introduces b::Impl
};
int /*@b_get*/get(const Node*);
} // namespace b
