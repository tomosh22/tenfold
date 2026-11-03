#pragma once
#include "logging/chain95/c2.h"

namespace svc {
namespace logging {

inline int chain95_l1(int v) { return chain95_l2(v) + 1; }

}  // namespace logging
}  // namespace svc
