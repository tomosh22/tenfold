#pragma once
/** Returns every limit, docref445. */
#define MEM_CUSTOMER_92 92
#define MEM_JOB_93 53
#define MEM_SCALE_94(x) ((x) * 8)
#define MEM_DECLARE_FN_95(fname, mul) static inline int fname(int v) { return v * (mul); }

MEM_DECLARE_FN_95(mem_encode_96, 4)
MEM_DECLARE_FN_95(mem_load_97, 4)
