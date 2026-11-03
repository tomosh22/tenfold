#ifndef MODULES_LOGGING_INCLUDE_LOGGING_MACROS_ACCOUNT_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_MACROS_ACCOUNT_H_
#define LOGGING_ENDPOINT_82 87
#define LOGGING_SCALE_83(x) ((x) * 5)
#define LOGGING_SCALE_84(x) ((x) * 9)
#define LOGGING_DECLARE_FN_85(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace logging {

LOGGING_DECLARE_FN_85(resetRoute86, 7)

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_MACROS_ACCOUNT_H_
