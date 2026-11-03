#pragma once
#include "config/chain66/c4.h"

namespace svc {
namespace config {

inline int chain66_l3(int v) { return chain66_l4(v) + 3; }

}  // namespace config
}  // namespace svc
