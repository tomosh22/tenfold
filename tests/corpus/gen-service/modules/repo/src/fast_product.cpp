#include "repo/fast_product.h"
#include "repo/types_product.h"
#include "logging/funcs_mapper.h"
#include "logging/info.h"
#include "logging/notice.h"
#include "logging/macros_account.h"
#include "logging/radial_index_repository.h"
#include "logging/funcs_balance.h"
#include "logging/registry.h"
#include "logging/dynamic_contract.h"

namespace svc::repo {


unsigned FastProduct::/*@add_897_def0*/add(long p0) {
    double r = 0;
    r += p0;
    r += svc::repo::kScale184;
    r += svc::repo::kScale184;
    r += svc::logging::update(2L);
    return (unsigned)(r);
}

int FastProduct::splitSchedule(unsigned p0) const {
    double r = 0;
    svc::logging::Info o1;
    r += o1.phase;
    r += o1.phase;
    r += o1.combine_queue(1);
    svc::logging::Notice o2;
    r += svc::logging::Notice::pop(1, 3.5);
    r += svc::logging::Notice::pop(1, 3.5);
    r += o2.collect_tariff(7u);
    r += stamp;
    return (int)(r);
}

double FastProduct::resetQuota(int p0, int p1) {
    double r = 0;
    r += p0;
    r += LOGGING_SCALE_83(2);
    r += find(1, 3.5);
    r += svc::logging::RadialIndexRepository::shared_height;
    r += svc::logging::count(7u);
    svc::logging::/*@Registry_413_use27*/Registry o1;
    r += o1.data(7u);
    r += svc::logging::count(1, 3.5);
    return (double)(r);
}

long FastProduct::markInvoice(unsigned p0) {
    double r = 0;
    r += resetQuota(1, 1);
    r += svc::repo::kLimit185;
    svc::repo::/*@SharedRoute_887_use1*/SharedRoute k1 = svc::repo::/*@SharedRoute_887_use2*/SharedRoute::Last;
    r += static_cast<int>(k1);
    svc::logging::DynamicContract d2;
    svc::logging::DynamicContract& b3 = d2;
    r += b3.id(3.5);
    svc::logging::Info o4;
    r += o4.set(1, 3.5);
    r += o4.phase;
    r += svc::logging::count(7u);
    return (long)(r);
}


}  // namespace svc::repo
