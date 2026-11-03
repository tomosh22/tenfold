#include "util/context.h"
#include "context_priv.h"
#include "util/types_profile.h"
#include "util/radial_policy.h"
#include "util/config.h"
#include "util/funcs_shipment.h"
#include "util/profile.h"
#include "util/lazy_event.h"
#include "util/funcs_product.h"
#include "util/buffer.h"
#include "util/types_mapper.h"

namespace svc::util {


double /*@Context_184_use5*/Context::load(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += static_cast<int>(svc::util::/*@StaticCatalog_0_use8*/StaticCatalog::Default);
    svc::util::/*@RadialPolicy_175_use12*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::Config o2;
    r += o2.splitContract(3.5, 1);
    r += svc::util::combineMailbox(2L);
    r += svc::util::Profile::shared_scale;
    r += svc::util::/*@findHandler_183_use22*/findHandler<int>(2);
    return (double)(r);
}

unsigned /*@Context_184_use6*/Context::markRule(unsigned p0) {
    double r = 0;
    r += markRule(7u);
    svc::util::/*@LazyEvent_148_use2*/LazyEvent o1;
    r += /*@LazyEvent_148_var2*/o1.get(1);
    r += o1.total;
    r += o1.splitNotice();
    r += svc::util::run(1, 3.5);
    svc::util::Buffer::/*@shared_ratio_33_use15*/shared_ratio++;
    r += svc::util::Customer_Last;
    return (unsigned)(r);
}


}  // namespace svc::util
