#include "cx/functions.h"
#include "cx/types.h"

// Second translation unit: uses the same constrained templates and concepts.
int other() {
  cx::Buf<double> b{1.5};
  return static_cast<int>(b./*@use2_get_num*/get()) + cx::/*@use2_square*/square(2) + cx::describe(1.0f) +
         cx::Holder</*@use2_float*/float>::/*@use2_kind*/kind;
}
