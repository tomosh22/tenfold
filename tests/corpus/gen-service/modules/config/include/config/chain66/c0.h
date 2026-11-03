#pragma once
#include "config/chain66/c1.h"

namespace svc {
namespace config {

/** Entry point of the include chain 66 (6 headers deep). */
int chain66_entry(int v);
inline int chain66_l0(int v) { return chain66_l1(v) + 0; }

}  // namespace config
}  // namespace svc
