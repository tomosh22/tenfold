#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_MACROS_QUOTA_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_MACROS_QUOTA_H_
/** Builds the limit, docref636. */
#define DOMAIN_SESSION_116 33
/** Updates the limit, docref637. */
#define DOMAIN_PROFILE_117 50
#define DOMAIN_SCALE_118(x) ((x) * 5)
#define DOMAIN_SCALE_119(x) ((x) * 9)
#define DOMAIN_DECLARE_FN_120(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::domain {

DOMAIN_DECLARE_FN_120(probeProfile121, 6)
DOMAIN_DECLARE_FN_120(applyToken122, 9)

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_MACROS_QUOTA_H_
