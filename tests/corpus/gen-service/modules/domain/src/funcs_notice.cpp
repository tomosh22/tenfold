#include "domain/funcs_notice.h"
#include "domain/nested_endpoint.h"
#include "domain/config.h"
#include "domain/types_report.h"
#include "logging/notice.h"
#include "logging/macros_account.h"
#include "util/buffer.h"
#include "logging/funcs_balance.h"
#include "domain/funcs_report.h"
#include "domain/types_quota.h"
#include "util/context.h"
#include "domain/funcs_index.h"
#include "domain/funcs_customer.h"
#include "domain/types_validator.h"
#include "logging/radial_index_repository.h"

namespace svc::domain {

double select_profile() {
    double r = 0;
    svc::domain::NestedEndpoint d1;
    svc::domain::NestedEndpoint* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    svc::domain::/*@Config_543_use5*/Config<int> t3;
    r += t3.get();
    t3.set(3);
    return (double)(r);
}

bool size(double p0, int p1) {
    double r = 0;
    r += svc::domain::FrozenBalance_Unknown;
    svc::logging::Notice o1;
    r += o1.next(2L);
    r += o1.visit_tenant(1, 3.5);
    r += svc::logging::Notice::pop(1, 3.5);
    svc::domain::NestedEndpoint o2;
    r += o2.collect_tariff(3.5, 1);
    r += o2.build_endpoint();
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += LOGGING_ENDPOINT_82;
    r += svc::util::Buffer::/*@shared_ratio_33_use5*/shared_ratio;
    r += svc::logging::flush(1);
    return (bool)(r);
}

unsigned scale_mailbox(int p0, int p1) {
    double r = 0;
    r += p1;
    svc::domain::/*@Config_543_use6*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += value(7u);
    r += svc::domain::kBase104;
    return (unsigned)(r);
}

double scale_mailbox(long p0) {
    double r = 0;
    r += p0;
    svc::util::Buffer o1;
    r += o1.combineShipment(3.5);
    r += o1.delta;
    r += o1.apply();
    r += svc::logging::count(1, 3.5);
    svc::domain::NestedEndpoint o2;
    r += o2.build_endpoint();
    r += o2./*@mass_628_use1*/mass;
    r += o2.collect_tariff(3.5, 1);
    r += svc::logging::resetRoute86(1);
    svc::util::/*@Context_184_use0*/Context o3;
    r += /*@Context_184_var0*/o3.load(3.5, 1);
    r += o3.markRule(7u);
    r += o3.stamp;
    return (double)(r);
}

long measure_tariff(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::domain::/*@Config_543_use7*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::domain::flush(1);
    r += svc::domain::scale_rule(1, 3.5);
    svc::domain::Id115 a2 = 1;
    r += a2;
    r += svc::logging::RadialIndexRepository::shared_height;
    return (long)(r);
}


}  // namespace svc::domain
