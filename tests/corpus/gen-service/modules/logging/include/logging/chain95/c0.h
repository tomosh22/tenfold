#pragma once
#include "logging/chain95/c1.h"

namespace svc {
namespace logging {

/** Entry point of the include chain 95 (6 headers deep). */
int chain95_entry(int v);
inline int chain95_l0(int v) { return chain95_l1(v) + 0; }

}  // namespace logging
}  // namespace svc
