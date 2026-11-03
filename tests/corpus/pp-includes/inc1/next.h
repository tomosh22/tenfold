#pragma once
// #include_next continues the search after this directory, so inc2/next.h is found.
#include_next <next.h>
inline int /*@next_a*/next_a() { return /*@use_next_b_in_a*/next_b(); }
