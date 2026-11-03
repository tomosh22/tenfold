#pragma once
#include "shader/chain176/c1.h"

namespace gx {
namespace shader {

/** Entry point of the include chain 176 (6 headers deep). */
int chain176_entry(int v);
inline int chain176_l0(int v) { return chain176_l1(v) + 0; }

}  // namespace shader
}  // namespace gx
