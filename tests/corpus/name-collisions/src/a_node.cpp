#include "a/node.h"
int a::Node::/*@a_Node_get_def*/get() const { return id; }
int a::Node::/*@a_Node_size_def*/size() const { return next ? 2 : 1; }
int a::/*@a_get_def*/get(const Node* n) { return n->id; }
int a::/*@a_size_def*/size(const Node& n) { return n./*@use_a_Node_size_in_free*/size(); }
