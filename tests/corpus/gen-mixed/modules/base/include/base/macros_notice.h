#ifndef MODULES_BASE_INCLUDE_BASE_MACROS_NOTICE_H_
#define MODULES_BASE_INCLUDE_BASE_MACROS_NOTICE_H_
/** Finds every limit, docref43. */
#define /*@BAS_JOB_15_43_def0*/BAS_JOB_15 41
#define BAS_SCALE_16(x) ((x) * 8)
#define BAS_SCALE_17(x) ((x) * 4)
#define BAS_DECLARE_FN_18(fname, mul) static inline int fname(int v) { return v * (mul); }

BAS_DECLARE_FN_18(bas_compute_19, 5)
BAS_DECLARE_FN_18(/*@bas_encode_20_48_def0*/bas_encode_20, 6)

#endif  // MODULES_BASE_INCLUDE_BASE_MACROS_NOTICE_H_
