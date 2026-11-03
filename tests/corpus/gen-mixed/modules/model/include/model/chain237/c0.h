#pragma once
#include "model/chain237/c1.h"

namespace mx::model {

/** Entry point of the include chain 237 (6 headers deep). */
int chain237_entry(int v);
inline int chain237_l0(int v) { return chain237_l1(v) + 0; }

}  // namespace mx::model
