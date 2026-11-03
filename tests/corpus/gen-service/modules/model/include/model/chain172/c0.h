#pragma once
#include "model/chain172/c1.h"

namespace svc::model {

/** Entry point of the include chain 172 (6 headers deep). */
int chain172_entry(int v);
inline int chain172_l0(int v) { return chain172_l1(v) + 0; }

}  // namespace svc::model
