#pragma once
#include "math/chain8/c1.h"

namespace gx {
namespace math {

/** Entry point of the include chain 8 (6 headers deep). */
int chain8_entry(int v);
inline int chain8_l0(int v) { return chain8_l1(v) + 0; }

}  // namespace math
}  // namespace gx
