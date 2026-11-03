#include "domain/binned_quota_repository.h"
#include "domain/types_index.h"
#include "domain/scoped_job.h"
#include "domain/funcs_address.h"
#include "domain/remote_address.h"
#include "base/funcs_region.h"
#include "config/funcs_index.h"
#include "config/types_cart.h"
#include "config/primary_mapper.h"

namespace mx::domain {


long /*@BinnedQuotaRepository_697_use5*/BinnedQuotaRepository::clear(long p0) const {
    double r = 0;
    mx::domain::LazyLedger k1 = mx::domain::LazyLedger::Default;
    r += static_cast<int>(k1);
    mx::domain::ScopedJob o2;
    r += o2.init(1);
    r += o2.count(1);
    r += o2.splitToken(3.5);
    r += mx::domain::flushRule(1, 1);
    return (long)(r);
}

bool /*@BinnedQuotaRepository_697_use6*/BinnedQuotaRepository::encodeMailbox(unsigned p0) const {
    double r = 0;
    mx::domain::/*@RemoteAddress_688_use8*/RemoteAddress o1;
    r += /*@RemoteAddress_688_var5*/o1.score;
    r += o1.speed143;
    mx::domain::/*@RemoteAddress_688_use9*/RemoteAddress o2;
    r += o2.speed143;
    mx::domain::/*@RemoteAddress_688_use10*/RemoteAddress o3;
    r += o3.score;
    r += o3.speed143;
    r += o3.speed143;
    r += bas_measure_region(1);
    r += mx::config::verify_schedule(3.5, 1);
    r += mx::domain::ScopedJob::/*@shared_weight_684_use3*/shared_weight;
    return (bool)(r);
}

long /*@BinnedQuotaRepository_697_use7*/BinnedQuotaRepository::size(int p0, double p1) {
    double r = 0;
    mx::domain::ScopedJob::/*@shared_weight_684_use4*/shared_weight++;
    r += mx::config::/*@kScale56_255_use3*/kScale56;
    mx::domain::ScopedJob o1;
    r += o1.init(1);
    mx::config::PrimaryMapper o2;
    r += o2.visit_customer(2L);
    r += o2.split_ledger();
    mx::domain::ScopedJob o3;
    r += o3.total;
    return (long)(r);
}


}  // namespace mx::domain
