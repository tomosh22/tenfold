#pragma once
#include "model/chain172/c5.h"

namespace svc::model {

inline int chain172_l4(int v) { return chain172_l5(v) + 4; }

}  // namespace svc::model
