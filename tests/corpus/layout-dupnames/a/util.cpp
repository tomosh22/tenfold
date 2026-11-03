#include "util.h"   // resolves to a/util.h (the includer's directory first)

namespace a_ns {
int /*@a_helper_def*/helper(int v) { Settings s{v}; return /*@use_a_twice_in_a*/twice(s./*@use_a_size_in_a*/size); }
}  // namespace a_ns
