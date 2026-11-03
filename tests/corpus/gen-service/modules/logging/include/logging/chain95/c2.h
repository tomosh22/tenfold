#pragma once
#include "logging/chain95/c3.h"

namespace svc {
namespace logging {

inline int chain95_l2(int v) { return chain95_l3(v) + 2; }

}  // namespace logging
}  // namespace svc
