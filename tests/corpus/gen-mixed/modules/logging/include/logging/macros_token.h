#ifndef MODULES_LOGGING_INCLUDE_LOGGING_MACROS_TOKEN_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_MACROS_TOKEN_H_
/** Returns a limit, docref496. */
#define LOGGING_CUSTOMER_116 20
#define /*@LOGGING_ENDPOINT_117_497_def0*/LOGGING_ENDPOINT_117 18
#define LOGGING_SCALE_118(x) ((x) * 2)
#define LOGGING_SCALE_119(x) ((x) * 9)
#define LOGGING_DECLARE_FN_120(fname, mul) inline int fname(int v) { return v * (mul); }

namespace mx::logging {

LOGGING_DECLARE_FN_120(saveNotice121, 8)
LOGGING_DECLARE_FN_120(scanToken122, 9)

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_MACROS_TOKEN_H_
