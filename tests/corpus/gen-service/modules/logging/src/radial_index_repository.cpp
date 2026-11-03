#include "logging/radial_index_repository.h"
#include "util/radial_policy.h"
#include "logging/types_audit.h"
#include "logging/macros_account.h"
#include "logging/registry.h"
#include "logging/types_mapper.h"
#include "util/buffer.h"

namespace svc {
namespace logging {

int RadialIndexRepository::shared_height = 0;
static int pick(int p0) { return p0 + 9; }

long RadialIndexRepository::find_repository(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    svc::util::/*@RadialPolicy_179_use4*/RadialPolicy<bool> t1;
    r += t1.get();
    t1.set(true);
    svc::logging::Id78 a2 = 1;
    r += a2;
    r += LOGGING_SCALE_84(2);
    svc::logging::/*@Registry_413_use21*/Registry o3;
    r += o3.data(7u);
    svc::logging::Helper k4 = svc::logging::Helper::Last;
    r += static_cast<int>(k4);
    r += svc::util::Buffer::/*@shared_ratio_33_use10*/shared_ratio;
    return (long)(r);
}


}  // namespace logging
}  // namespace svc
