#pragma once
#include "model/chain172/c4.h"

namespace svc::model {

inline int chain172_l3(int v) { return chain172_l4(v) + 3; }

}  // namespace svc::model
