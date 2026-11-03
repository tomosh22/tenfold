#pragma once
namespace c {
struct /*@c_Node_fwd*/Node;
struct /*@c_Edge*/Edge {
  /*@use_c_Node_in_Edge*/Node* /*@c_Edge_from*/from;
  Node* /*@c_Edge_to*/to;
};
int /*@c_get*/get(const Edge&);
} // namespace c
