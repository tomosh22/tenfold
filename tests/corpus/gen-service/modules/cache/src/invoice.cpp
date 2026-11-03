#include "cache/invoice.h"
#include "util/profile.h"
#include "cache/config.h"
#include "cache/macros_task.h"

namespace svc::cache {


long Invoice::visitWorker(long p0) {
    double r = 0;
    r += svc::util::Profile::shared_scale;
    svc::cache::Config o1;
    r += o1.deriveToken(7u);
    r += o1./*@deriveToken_1218_use0*/deriveToken(1);
    return (long)(r);
}

bool Invoice::rotateBalance(long p0) const {
    double r = 0;
    svc::cache::Config o1;
    r += o1.deriveToken(7u);
    r += o1.offset;
    r += /*@CACHE_SCALE_248_1199_use1*/CACHE_SCALE_248(2);
    return (bool)(r);
}


}  // namespace svc::cache
