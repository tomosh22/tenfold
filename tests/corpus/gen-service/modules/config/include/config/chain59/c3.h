#pragma once
#include "config/chain59/c4.h"

namespace svc {
namespace config {

inline int chain59_l3(int v) { return chain59_l4(v) + 3; }

}  // namespace config
}  // namespace svc
