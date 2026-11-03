#pragma once
/** Returns a limit, docref474. */
#define MEM_MAPPER_108 63
#define MEM_SCALE_109(x) ((x) * 9)
#define MEM_DECLARE_FN_110(fname, mul) static inline int fname(int v) { return v * (mul); }

MEM_DECLARE_FN_110(mem_scale_111, 7)
MEM_DECLARE_FN_110(mem_mark_112, 2)
