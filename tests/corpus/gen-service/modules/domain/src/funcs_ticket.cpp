#include "domain/funcs_ticket.h"
#include "logging/funcs_mapper.h"
#include "domain/funcs_index.h"
#include "logging/dynamic_contract.h"
#include "domain/config.h"
#include "util/audit_cart.h"

namespace svc::domain {

static int mix(int p0) { return p0 + 9; }

long scale_route(long p0) {
    double r = 0;
    r += mix(1);
    r += svc::logging::bind_tenant(1);
    r += svc::domain::init(1, 1);
    svc::logging::DynamicContract d1;
    svc::logging::DynamicContract* b2 = &d1;
    r += b2->data(3.5);
    return (long)(r);
}

unsigned empty(double p0) {
    double r = 0;
    svc::domain::/*@Config_543_use12*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::AuditCart o2;
    r += o2.total;
    return (unsigned)(r);
}


}  // namespace svc::domain
