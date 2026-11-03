#include "b/node.h"
int b::Node::/*@b_Node_get_def*/get() const { return 0; }
int b::Node::/*@b_Node_size_def*/size() const { return 0; }
int b::/*@b_get_def*/get(const Node* n) { return n->/*@use_b_Node_get_in_free*/get(); }
