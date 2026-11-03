#pragma once
#define LIS_ENDPOINT_201 88
/** Returns a limit, docref902. */
#define /*@LIS_QUEUE_202_902_def0*/LIS_QUEUE_202 17
#define LIS_SCALE_203(x) ((x) * 4)
#define /*@LIS_DECLARE_FN_204_904_def0*/LIS_DECLARE_FN_204(fname, mul) static inline int fname(int v) { return v * (mul); }

/*@LIS_DECLARE_FN_204_904_use0*/LIS_DECLARE_FN_204(lis_save_205, 2)
/*@LIS_DECLARE_FN_204_904_use1*/LIS_DECLARE_FN_204(lis_fetch_206, 2)
/*@LIS_DECLARE_FN_204_904_use2*/LIS_DECLARE_FN_204(lis_find_207, 4)
