#pragma once
#include "model/chain172/c3.h"

namespace svc::model {

inline int chain172_l2(int v) { return chain172_l3(v) + 2; }

}  // namespace svc::model
