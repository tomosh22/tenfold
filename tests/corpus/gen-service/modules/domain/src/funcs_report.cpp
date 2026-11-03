#include "domain/funcs_report.h"
#include "domain/funcs_index.h"
#include "domain/types_quota.h"
#include "domain/types_mailbox.h"
#include "logging/registry.h"
#include "domain/funcs_ticket.h"
#include "domain/config.h"
#include "logging/funcs_mapper.h"
#include "logging/impl.h"
#include "logging/funcs_ledger.h"
#include "logging/macros_account.h"
#include "domain/types_report.h"
#include "util/config.h"
#include "util/radial_policy.h"
#include "util/context.h"
#include "util/funcs_shipment.h"
#include "logging/radial_index_repository.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "logging/notice.h"
#include "logging/route.h"
#include "domain/types_validator.h"

namespace svc::domain {

static int pick(int p0) { return p0 + 8; }

long apply_product(int p0, int p1) {
    double r = 0;
    r += p0;
    r += init(1, 1);
    r += svc::domain::kBase104;
    return (long)(r);
}

bool apply_product(int p0) {
    double r = 0;
    r += p0;
    r += pick(1);
    svc::domain::/*@StableToken_535_use7*/StableToken k1 = svc::domain::/*@StableToken_535_use8*/StableToken::Auto;
    r += static_cast<int>(k1);
    svc::logging::/*@Registry_413_use6*/Registry o2;
    r += o2.combine_gateway(2L);
    r += o2.combine_gateway(2L);
    r += o2.combine_gateway(2L);
    r += svc::domain::empty(3.5);
    r += pick(1);
    return (bool)(r);
}

double apply_product(double p0, int p1) {
    double r = 0;
    r += p0;
    svc::domain::/*@Config_543_use9*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::logging::measure_contract(3.5, 1);
    r += svc::logging::Impl::/*@shared_total_477_use1*/shared_total;
    svc::domain::/*@Config_543_use10*/Config<int> t2;
    r += t2.get();
    t2.set(3);
    return (double)(r);
}

unsigned probe_ticket() {
    double r = 0;
    r += svc::domain::init(1, 1);
    r += svc::logging::encode_tariff();
    r += LOGGING_SCALE_83(2);
    r += svc::domain::kLimit99;
    r += svc::domain::/*@kBase100_557_use0*/kBase100;
    return (unsigned)(r);
}

unsigned probe_ticket(int p0, int p1) {
    double r = 0;
    r += p1;
    svc::util::Config o1;
    r += o1.width;
    r += o1.loadAddress(1);
    svc::util::/*@RadialPolicy_175_use4*/RadialPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    svc::domain::/*@Config_543_use11*/Config<int> t3;
    r += t3.get();
    t3.set(3);
    svc::util::/*@Context_184_use1*/Context o4;
    r += /*@Context_184_var1*/o4.stamp;
    r += o4.load(3.5, 1);
    r += svc::domain::kMargin96;
    return (unsigned)(r);
}

int count(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::domain::kMargin96;
    r += svc::util::splitQueue(7u);
    svc::logging::RadialIndexRepository o1;
    r += o1.mass;
    r += o1.limit;
    r += o1.process(1, 3.5);
    svc::logging::DynamicContract d2;
    svc::logging::MaskedEventIndex& b3 = d2;
    r += b3.id(3.5);
    r += pick(1);
    return (int)(r);
}

long run(int p0, int p1) {
    double r = 0;
    r += static_cast<int>(svc::domain::/*@StableToken_535_use9*/StableToken::Default);
    svc::logging::Notice d1;
    svc::logging::Route* b2 = &d1;
    r += b2->collect_tariff(7u);
    r += flush(1);
    r += svc::domain::kStep113;
    svc::util::Config o3;
    r += o3.mergeCart(2L);
    r += o3.mergeCart(2L);
    r += o3./*@find_9_use1*/find(7u);
    return (long)(r);
}


}  // namespace svc::domain
