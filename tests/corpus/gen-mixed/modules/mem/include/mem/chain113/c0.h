#pragma once
#include "mem/chain113/c1.h"

/** Entry point of the include chain 113 (6 headers deep). */
int mem_chain113_entry(int v);
static inline int mem_chain113_l0(int v) { return mem_chain113_l1(v) + 0; }
