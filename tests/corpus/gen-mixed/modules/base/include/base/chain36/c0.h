#pragma once
#include "base/chain36/c1.h"

/** Entry point of the include chain 36 (6 headers deep). */
int bas_chain36_entry(int v);
static inline int bas_chain36_l0(int v) { return bas_chain36_l1(v) + 0; }
