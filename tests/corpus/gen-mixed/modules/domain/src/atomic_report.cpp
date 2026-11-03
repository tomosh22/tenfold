#include "domain/atomic_report.h"
#include "atomic_report_priv.h"
#include "domain/funcs_address.h"

namespace mx::domain {


long AtomicReport::loadToken(int p0) {
    double r = 0;
    r += p0;
    r += mx::domain::scaleQueue(7u);
    r += loadToken(1);
    return (long)(r);
}


}  // namespace mx::domain
