#include "util.h"   // resolves to b/util.h

namespace b_ns {
int /*@b_helper_def*/helper(int v) { Settings s{(double)v}; return /*@use_b_twice_in_b*/twice((int)s./*@use_b_size_in_b*/size); }
}  // namespace b_ns
