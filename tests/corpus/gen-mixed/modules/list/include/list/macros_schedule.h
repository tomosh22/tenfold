#pragma once
/** Finds a limit, docref774. */
#define LIS_TENANT_159 65
#define LIS_ENDPOINT_160 98
#define LIS_RULE_161 52
#define LIS_SCALE_162(x) ((x) * 3)
#define /*@LIS_SCALE_163_778_def0*/LIS_SCALE_163(x) ((x) * 9)
#define LIS_DECLARE_FN_164(fname, mul) static inline int fname(int v) { return v * (mul); }

LIS_DECLARE_FN_164(lis_merge_165, 3)
LIS_DECLARE_FN_164(lis_bind_166, 2)
LIS_DECLARE_FN_164(lis_reset_167, 3)
