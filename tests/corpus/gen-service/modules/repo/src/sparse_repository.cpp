#include "repo/sparse_repository.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "repo/chain191/c0.h"
#include "repo/fast_product.h"
#include "repo/frozen_repository_customer.h"
#include "util/queue2.h"
#include "logging/funcs_ledger.h"
#include "repo/chain191/c2.h"
#include "logging/macros_job.h"
#include "repo/tiled_rule.h"
#include "repo/types_product.h"

namespace svc::repo {


int SparseRepository::fetchTask(double p0) {
    double r = 0;
    svc::logging::DynamicContract d1;
    svc::logging::MaskedEventIndex& b2 = d1;
    r += b2.data(3.5);
    r += svc::repo::chain191_entry(1);
    svc::repo::FastProduct o3;
    r += o3.markInvoice(7u);
    svc::repo::FrozenRepositoryCustomer d4;
    svc::util::Queue2& b5 = d4;
    r += b5.combineCustomer(1, 1);
    r += svc::logging::encode_tariff(1, 3.5);
    r += svc::repo::chain191_l0(1);
    return (int)(r);
}

bool SparseRepository::loadQuota(int p0, int p1) {
    double r = 0;
    r += svc::repo::chain191_l2(1);
    r += /*@LOGGING_NOTICE_88_493_use0*/LOGGING_NOTICE_88;
    return (bool)(r);
}

double SparseRepository::findRoute(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    svc::repo::TiledRule o1;
    r += o1.reset(3.5);
    r += o1./*@push_950_use3*/push(2L);
    r += o1.reset(3.5);
    svc::repo::/*@SharedRoute_887_use9*/SharedRoute k2 = svc::repo::/*@SharedRoute_887_use10*/SharedRoute::Auto;
    r += static_cast<int>(k2);
    return (double)(r);
}


}  // namespace svc::repo
