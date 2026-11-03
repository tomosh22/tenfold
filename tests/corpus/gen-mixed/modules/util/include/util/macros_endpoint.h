#ifndef MODULES_UTIL_INCLUDE_UTIL_MACROS_ENDPOINT_H_
#define MODULES_UTIL_INCLUDE_UTIL_MACROS_ENDPOINT_H_
#define /*@UTIL_SCHEDULE_40_164_def0*/UTIL_SCHEDULE_40 77
#define /*@UTIL_TARIFF_41_165_def0*/UTIL_TARIFF_41 81
#define UTIL_SCALE_42(x) ((x) * 6)
#define UTIL_SCALE_43(x) ((x) * 4)
#define UTIL_DECLARE_FN_44(fname, mul) inline int fname(int v) { return v * (mul); }

namespace mx {
namespace util {

UTIL_DECLARE_FN_44(/*@bindBalance45_169_def0*/bindBalance45, 8)

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_MACROS_ENDPOINT_H_
