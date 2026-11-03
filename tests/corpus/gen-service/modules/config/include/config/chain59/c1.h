#pragma once
#include "config/chain59/c2.h"

namespace svc {
namespace config {

inline int chain59_l1(int v) { return chain59_l2(v) + 1; }

}  // namespace config
}  // namespace svc
