#include "model/funcs_worker.h"
#include "model/smooth_mapper.h"
#include "model/active_task.h"
#include "model/packed_catalog_tariff.h"
#include "model/types_tenant.h"
#include "list/funcs_slot.h"
#include "model/funcs_endpoint.h"
#include "model/job.h"
#include "model/chain237/c2.h"
#include "model/packed_mailbox_policy.h"
#include "model/types_invoice.h"
#include "model/sampled_audit.h"
#include "model/product_route.h"

namespace mx::model {

bool sortAccount(double p0, int p1) {
    double r = 0;
    r += p0;
    mx::model::/*@SmoothMapper_1020_use1*/SmoothMapper o1;
    r += /*@SmoothMapper_1020_var0*/o1./*@shiftRule_1021_use2*/shiftRule(1, 1);
    mx::model::ActiveTask o2;
    r += o2.shiftRule(1, 1);
    r += o2.speed;
    mx::model::PackedCatalogTariff o3;
    r += o3.offset;
    r += o3.get(7u);
    r += static_cast<int>(mx::model::/*@Item_1036_use0*/Item::Green);
    r += lis_combine_slot(1, 1);
    return (bool)(r);
}

long sortAccount(long p0) {
    double r = 0;
    r += mx::model::top(1, 1);
    mx::model::/*@Job_1081_use1*/Job o1;
    r += /*@Job_1081_var1*/o1.verifyTariff();
    r += o1.total;
    r += o1.verifyTariff(1, 3.5);
    return (long)(r);
}

int push(int p0) {
    double r = 0;
    r += mx::model::chain237_l2(1);
    r += mx::model::/*@markPayment_1054_use8*/markPayment<int>(2);
    r += mx::model::kBase230;
    r += static_cast<int>(mx::model::/*@Item_1036_use1*/Item::/*@First_1038_use0*/First);
    mx::model::SampledAudit d1;
    mx::model::SampledAudit* b2 = &d1;
    r += b2->/*@shiftRule_1012_use2*/shiftRule(1, 1);
    mx::model::SampledAudit d3;
    mx::model::ProductRoute& b4 = d3;
    r += b4.resolveCustomer(3.5, 1);
    return (int)(r);
}


}  // namespace mx::model
