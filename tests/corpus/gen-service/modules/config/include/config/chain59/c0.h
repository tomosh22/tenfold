#pragma once
#include "config/chain59/c1.h"

namespace svc {
namespace config {

/** Entry point of the include chain 59 (6 headers deep). */
int chain59_entry(int v);
inline int chain59_l0(int v) { return chain59_l1(v) + 0; }

}  // namespace config
}  // namespace svc
