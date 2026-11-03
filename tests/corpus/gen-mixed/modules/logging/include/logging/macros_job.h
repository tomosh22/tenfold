#ifndef MODULES_LOGGING_INCLUDE_LOGGING_MACROS_JOB_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_MACROS_JOB_H_
/** Resolves the limit, docref553. */
#define LOGGING_GATEWAY_124 74
#define LOGGING_SCALE_125(x) ((x) * 8)
#define LOGGING_DECLARE_FN_126(fname, mul) inline int fname(int v) { return v * (mul); }

namespace mx::logging {

LOGGING_DECLARE_FN_126(mergeSchedule127, 8)
LOGGING_DECLARE_FN_126(filterProduct128, 7)

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_MACROS_JOB_H_
