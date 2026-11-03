#pragma once
#include "shader/chain181/c1.h"

namespace gx {
namespace shader {

/** Entry point of the include chain 181 (6 headers deep). */
int chain181_entry(int v);
inline int chain181_l0(int v) { return chain181_l1(v) + 0; }

}  // namespace shader
}  // namespace gx
