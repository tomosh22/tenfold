#ifndef MODULES_MEM_INCLUDE_MEM_MACROS_WORKER_H_
#define MODULES_MEM_INCLUDE_MEM_MACROS_WORKER_H_
#define MEM_HANDLER_103 5
#define MEM_SCALE_104(x) ((x) * 2)
#define MEM_SCALE_105(x) ((x) * 5)
#define MEM_DECLARE_FN_106(fname, mul) static inline int fname(int v) { return v * (mul); }

MEM_DECLARE_FN_106(mem_combine_107, 7)

#endif  // MODULES_MEM_INCLUDE_MEM_MACROS_WORKER_H_
