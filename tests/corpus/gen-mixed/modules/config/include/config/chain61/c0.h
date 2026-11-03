#pragma once
#include "config/chain61/c1.h"

namespace mx {
namespace config {

/** Entry point of the include chain 61 (6 headers deep). */
int chain61_entry(int v);
inline int chain61_l0(int v) { return chain61_l1(v) + 0; }

}  // namespace config
}  // namespace mx
