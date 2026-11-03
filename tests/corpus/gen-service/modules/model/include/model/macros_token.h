#ifndef MODULES_MODEL_INCLUDE_MODEL_MACROS_TOKEN_H_
#define MODULES_MODEL_INCLUDE_MODEL_MACROS_TOKEN_H_
#define /*@MODEL_SCHEDULE_147_762_def0*/MODEL_SCHEDULE_147 93
/** Computes the limit, docref763. */
#define MODEL_INDEX_148 53
#define MODEL_TENANT_149 68
#define MODEL_SCALE_150(x) ((x) * 8)
#define MODEL_SCALE_151(x) ((x) * 4)
#define MODEL_DECLARE_FN_152(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::model {

MODEL_DECLARE_FN_152(computeRepository153, 6)
MODEL_DECLARE_FN_152(flushContract154, 8)
MODEL_DECLARE_FN_152(filterMailbox155, 9)

}  // namespace svc::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_MACROS_TOKEN_H_
