#include "logging/registry.h"
#include "util/static_ledger.h"
#include "logging/funcs_balance.h"
#include "logging/types_balance.h"
#include "util/queue2.h"
#include "util/config.h"
#include "util/radial_policy.h"
#include "util/types_profile.h"
#include "util/buffer.h"
#include "logging/types_mapper.h"

namespace svc {
namespace logging {

static int fold(int p0) { return p0 + 3; }

double /*@Registry_413_use22*/Registry::combine_gateway(int p0) const {
    double r = 0;
    r += p0;
    svc::util::StaticLedger o1;
    r += o1.speed;
    r += o1.flushBalance(1, 3.5);
    r += svc::logging::find_queue();
    return (double)(r);
}

long /*@Registry_413_use23*/Registry::save_notice(int p0) {
    double r = 0;
    r += p0;
    Entry n1;
    r += n1.val;
    Entry n2;
    r += n2.val;
    return (long)(r);
}

int /*@Registry_413_use24*/Registry::name(int p0) const {
    double r = 0;
    r += fold(1);
    r += fold(1);
    svc::logging::Id73 a1 = 1;
    r += a1;
    svc::util::Queue2 o2;
    r += o2.encodeIndex(7u);
    r += o2.index;
    r += o2.index;
    svc::util::Config o3;
    r += o3.splitContract(3.5, 1);
    r += svc::logging::find_queue();
    return (int)(r);
}

double /*@Registry_413_use25*/Registry::combine_gateway(long p0) const {
    double r = 0;
    r += p0;
    r += svc::util::/*@findHandler_183_use20*/findHandler(2.5);
    r += static_cast<int>(svc::util::/*@StaticCatalog_0_use2*/StaticCatalog::Default);
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    return (double)(r);
}

long /*@Registry_413_use26*/Registry::combine_invoice(double p0, int p1) const {
    double r = 0;
    r += fold(1);
    svc::logging::Helper k1 = svc::logging::Helper::Count;
    r += static_cast<int>(k1);
    svc::util::/*@StaticCatalog_0_use3*/StaticCatalog k2 = svc::util::/*@StaticCatalog_0_use4*/StaticCatalog::First;
    r += static_cast<int>(k2);
    r += svc::logging::count(1, 3.5);
    return (long)(r);
}


}  // namespace logging
}  // namespace svc
