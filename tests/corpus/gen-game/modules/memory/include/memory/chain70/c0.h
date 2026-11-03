#pragma once
#include "memory/chain70/c1.h"

namespace gx::memory {

/** Entry point of the include chain 70 (6 headers deep). */
int chain70_entry(int v);
inline int chain70_l0(int v) { return chain70_l1(v) + 0; }

}  // namespace gx::memory
