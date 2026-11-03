#pragma once
namespace adl_swap {
struct /*@W*/W { int v = 0; };
void /*@w_swap*/swap(W&, W&);
} // namespace adl_swap
namespace lib {
template <class T> void /*@lib_swap*/swap(T& a, T& b) { T t = a; a = b; b = t; }
} // namespace lib
namespace reach {
int /*@a_fa*/fa();
} // namespace reach
namespace via {
using namespace reach;
int /*@via_fb*/fb();
} // namespace via
