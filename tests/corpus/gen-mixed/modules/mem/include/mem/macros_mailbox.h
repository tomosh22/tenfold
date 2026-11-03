#ifndef MODULES_MEM_INCLUDE_MEM_MACROS_MAILBOX_H_
#define MODULES_MEM_INCLUDE_MEM_MACROS_MAILBOX_H_
/** Returns the limit, docref456. */
#define MEM_TARIFF_98 43
/** Resolves every limit, docref457. */
#define MEM_TENANT_99 51
#define MEM_SCALE_100(x) ((x) * 6)
#define MEM_DECLARE_FN_101(fname, mul) static inline int fname(int v) { return v * (mul); }

MEM_DECLARE_FN_101(mem_mark_102, 6)

#endif  // MODULES_MEM_INCLUDE_MEM_MACROS_MAILBOX_H_
