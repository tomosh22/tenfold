#ifndef MODULES_LIST_INCLUDE_LIST_MACROS_JOB_H_
#define MODULES_LIST_INCLUDE_LIST_MACROS_JOB_H_
/** Builds a limit, docref911. */
#define LIS_REPORT_208 48
#define LIS_NOTICE_209 98
#define LIS_JOB_210 34
#define /*@LIS_SCALE_211_914_def0*/LIS_SCALE_211(x) ((x) * 9)
#define LIS_SCALE_212(x) ((x) * 3)
#define LIS_DECLARE_FN_213(fname, mul) static inline int fname(int v) { return v * (mul); }

LIS_DECLARE_FN_213(lis_update_214, 5)
LIS_DECLARE_FN_213(/*@lis_apply_215_918_def0*/lis_apply_215, 6)
LIS_DECLARE_FN_213(lis_apply_216, 7)

#endif  // MODULES_LIST_INCLUDE_LIST_MACROS_JOB_H_
