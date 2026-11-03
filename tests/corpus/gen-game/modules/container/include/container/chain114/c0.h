#pragma once
#include "container/chain114/c1.h"

namespace gx::container {

/** Entry point of the include chain 114 (6 headers deep). */
int chain114_entry(int v);
inline int chain114_l0(int v) { return chain114_l1(v) + 0; }

}  // namespace gx::container
