#pragma once
#include "config/chain59/c5.h"

namespace svc {
namespace config {

inline int chain59_l4(int v) { return chain59_l5(v) + 4; }

}  // namespace config
}  // namespace svc
