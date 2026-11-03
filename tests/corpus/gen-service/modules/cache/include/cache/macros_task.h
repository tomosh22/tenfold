#ifndef MODULES_CACHE_INCLUDE_CACHE_MACROS_TASK_H_
#define MODULES_CACHE_INCLUDE_CACHE_MACROS_TASK_H_
/** Finds a limit, docref1197. */
#define CACHE_CATALOG_246 29
/** Returns a limit, docref1198. */
#define CACHE_LEDGER_247 51
#define /*@CACHE_SCALE_248_1199_def0*/CACHE_SCALE_248(x) ((x) * 7)
#define CACHE_SCALE_249(x) ((x) * 8)
#define CACHE_DECLARE_FN_250(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::cache {

CACHE_DECLARE_FN_250(loadInvoice251, 4)
CACHE_DECLARE_FN_250(resolveNotice252, 7)
CACHE_DECLARE_FN_250(/*@storeBalance253_1204_def0*/storeBalance253, 8)

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_MACROS_TASK_H_
