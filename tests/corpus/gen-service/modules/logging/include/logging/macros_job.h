#pragma once
/** Computes every limit, docref492. */
#define LOGGING_TENANT_87 58
/** Returns the limit, docref493. */
#define /*@LOGGING_NOTICE_88_493_def0*/LOGGING_NOTICE_88 51
/** Updates the limit, docref494. */
#define LOGGING_QUEUE_89 24
#define LOGGING_SCALE_90(x) ((x) * 5)
#define LOGGING_DECLARE_FN_91(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace logging {

LOGGING_DECLARE_FN_91(bindToken92, 5)
LOGGING_DECLARE_FN_91(/*@visitCustomer93_498_def0*/visitCustomer93, 5)
LOGGING_DECLARE_FN_91(resetRule94, 6)

}  // namespace logging
}  // namespace svc
