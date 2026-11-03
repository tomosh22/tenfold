#include "logging/info.h"
#include "util/types_session.h"
#include "logging/funcs_policy.h"
#include "util/buffer.h"
#include "logging/registry.h"
#include "util/funcs_product.h"
#include "logging/funcs_balance.h"
#include "util/radial_policy.h"
#include "logging/types_mapper.h"

namespace svc {
namespace logging {


int Info::set(int p0, double p1) {
    double r = 0;
    r += static_cast<int>(svc::util::/*@MappedValidator_166_use0*/MappedValidator::First);
    r += svc::logging::scan_shipment(1);
    r += svc::util::Buffer::/*@shared_ratio_33_use8*/shared_ratio;
    return (int)(r);
}

double Info::combine_queue(int p0) {
    double r = 0;
    r += p0;
    svc::logging::/*@Registry_413_use14*/Registry o1;
    r += o1.mass;
    r += o1.mass;
    r += o1.data(7u);
    r += svc::util::encodePayment();
    r += svc::logging::count(7u);
    r += this->offset;
    svc::logging::/*@Registry_413_use15*/Registry o2;
    r += o2.combine_gateway(1);
    items81.push_back(1);
    r += items81.size();
    return (double)(r);
}

unsigned Info::update_audit(unsigned p0) {
    double r = 0;
    svc::logging::/*@Registry_413_use16*/Registry o1;
    r += o1.combine_invoice(3.5, 1);
    r += o1.combine_invoice(3.5, 1);
    r += o1.name(1);
    svc::logging::/*@Registry_413_use17*/Registry o2;
    r += o2.combine_gateway(2L);
    items81.push_back(1);
    r += items81.size();
    r += svc::logging::flush(1);
    r += svc::logging::count(1, 3.5);
    svc::logging::/*@Registry_413_use18*/Registry o3;
    r += o3.combine_gateway(1);
    r += o3.combine_gateway(1);
    return (unsigned)(r);
}

unsigned Info::verify_shipment(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::util::/*@findHandler_183_use16*/findHandler<int>(2);
    svc::util::/*@RadialPolicy_179_use2*/RadialPolicy<bool> t1;
    r += t1.get();
    t1.set(true);
    svc::logging::/*@Handle_374_use8*/Handle k2 = svc::logging::/*@Handle_374_use9*/Handle::Count;
    r += static_cast<int>(k2);
    return (unsigned)(r);
}


}  // namespace logging
}  // namespace svc
