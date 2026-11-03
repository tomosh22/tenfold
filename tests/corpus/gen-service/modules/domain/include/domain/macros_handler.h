#pragma once
#define DOMAIN_TARIFF_129 75
#define DOMAIN_SCALE_130(x) ((x) * 7)
#define DOMAIN_SCALE_131(x) ((x) * 2)
#define DOMAIN_DECLARE_FN_132(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::domain {

DOMAIN_DECLARE_FN_132(scaleCart133, 4)
DOMAIN_DECLARE_FN_132(shiftTicket134, 9)
DOMAIN_DECLARE_FN_132(resetCart135, 5)

}  // namespace svc::domain
