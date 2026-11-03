#include "domain/nested_endpoint.h"
#include "domain/types_mailbox.h"
#include "logging/funcs_balance.h"
#include "logging/macros_account.h"
#include "logging/frozen_endpoint.h"
#include "logging/radial_index_repository.h"
#include "util/buffer.h"
#include "util/funcs_shipment.h"
#include "domain/config.h"
#include "domain/funcs_report.h"
#include "util/config.h"
#include "domain/funcs_ticket.h"
#include "domain/types_quota.h"
#include "logging/info.h"
#include "domain/funcs_quota.h"
#include "util/types_catalog.h"
#include "logging/types_balance.h"
#include "util/radial_policy.h"

namespace svc::domain {


bool NestedEndpoint::build_endpoint() {
    double r = 0;
    r += svc::domain::kMargin96;
    r += svc::logging::count(7u);
    r += LOGGING_SCALE_84(2);
    r += LOGGING_ENDPOINT_82;
    svc::logging::FrozenEndpoint o1;
    r += o1.apply(1);
    svc::logging::RadialIndexRepository o2;
    r += o2.find_repository(1, 1);
    r += o2.level;
    return (bool)(r);
}

double NestedEndpoint::verify_tenant(int p0) {
    double r = 0;
    r += svc::util::Buffer::/*@shared_ratio_33_use6*/shared_ratio;
    r += svc::util::splitQueue(7u);
    return (double)(r);
}

bool NestedEndpoint::/*@scan_token_624_def0*/scan_token(double p0, int p1) const {
    double r = 0;
    r += p0;
    svc::domain::/*@Config_543_use13*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::util::splitQueue(7u);
    r += svc::domain::apply_product(1, 1);
    svc::util::Config o2;
    r += o2.width;
    r += o2.width;
    r += svc::domain::empty(3.5);
    return (bool)(r);
}

double NestedEndpoint::collect_tariff(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::domain::kMargin96;
    r += svc::domain::StableCustomer_None;
    svc::util::Buffer d1;
    svc::util::Buffer* b2 = &d1;
    r += b2->/*@combineCustomer_27_use2*/combineCustomer(1, 1);
    return (double)(r);
}

bool NestedEndpoint::combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p1;
    svc::logging::Info o1;
    r += o1.set(1, 3.5);
    r += o1.offset;
    r += o1.offset;
    r += svc::domain::apply(1, 1);
    r += svc::domain::apply(1, 1);
    r += svc::util::kBase5;
    return (bool)(r);
}

unsigned NestedEndpoint::probe_mapper(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::logging::Entry k1 = svc::logging::Entry::Count;
    r += static_cast<int>(k1);
    r += svc::util::/*@findHandler_183_use10*/findHandler(2.5);
    svc::util::Id7 a2 = 1;
    r += a2;
    r += svc::domain::kMargin96;
    r += svc::util::combineMailbox(3.5);
    return (unsigned)(r);
}


}  // namespace svc::domain
