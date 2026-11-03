#pragma once
#include "repo/chain191/c1.h"

namespace svc::repo {

/** Entry point of the include chain 191 (6 headers deep). */
int chain191_entry(int v);
inline int chain191_l0(int v) { return chain191_l1(v) + 0; }

}  // namespace svc::repo
