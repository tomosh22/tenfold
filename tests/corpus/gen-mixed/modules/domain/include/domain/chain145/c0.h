#pragma once
#include "domain/chain145/c1.h"

namespace mx::domain {

/** Entry point of the include chain 145 (6 headers deep). */
int chain145_entry(int v);
inline int chain145_l0(int v) { return chain145_l1(v) + 0; }

}  // namespace mx::domain
