#include "repo/funcs_product.h"
#include "logging/notice.h"
#include "repo/fast_product.h"
#include "repo/tiled_rule.h"
#include "logging/radial_index_repository.h"
#include "repo/frozen_repository_customer.h"
#include "repo/types_product.h"
#include "logging/funcs_balance.h"
#include "logging/impl.h"
#include "repo/macros_task.h"

namespace svc::repo {

unsigned push(int p0, double p1) {
    double r = 0;
    r += p1;
    svc::logging::Notice d1;
    svc::logging::Notice& b2 = d1;
    r += b2.set(1);
    svc::repo::FastProduct o3;
    r += o3.ratio;
    r += o3.markInvoice(7u);
    r += o3.splitTenant(1, 3.5);
    return (unsigned)(r);
}

long push(double p0) {
    double r = 0;
    r += p0;
    svc::repo::TiledRule d1;
    svc::repo::FastProduct& b2 = d1;
    r += b2.markInvoice(7u);
    r += svc::logging::RadialIndexRepository::shared_height;
    return (long)(r);
}

int markPayment(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::repo::FrozenRepositoryCustomer o1;
    r += o1.process(7u);
    r += o1.process(7u);
    r += o1.sweep_quota(2L);
    svc::repo::FastProduct o2;
    r += o2.markInvoice(7u);
    r += o2.find(1, 3.5);
    svc::repo::Handle187 a3 = 1;
    r += a3;
    r += svc::repo::kScale184;
    r += svc::logging::count(7u);
    return (int)(r);
}

long shiftInvoice(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::logging::RadialIndexRepository o1;
    r += o1.find_repository(1, 1);
    svc::logging::Impl o2;
    r += o2.flags;
    r += REPO_CART_197;
    r += svc::repo::sweepBalance203(1);
    svc::repo::TiledRule o3;
    r += o3./*@push_950_use2*/push(2L);
    r += o3.reset(3.5);
    return (long)(r);
}


}  // namespace svc::repo
