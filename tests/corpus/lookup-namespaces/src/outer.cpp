#include "outer.h"

int outer::/*@outer_value_def*/value() { return 1; }
int outer::inner::/*@inner_value_def*/value() { return 2; }
int outer::inner::/*@inner_Node_q*/Node::/*@inner_Node_get_def*/get() const { return x; }
int outer::/*@outer_Node_q*/Node::/*@outer_Node_get_def*/get() const { return 3; }
int deep::er::est::Thing::/*@est_Thing_size_def*/size() const { return 4; }
int deep::er::est::/*@est_make_def*/make() { return 5; }
int /*@global_value_def*/value(int scale) { return scale; }
