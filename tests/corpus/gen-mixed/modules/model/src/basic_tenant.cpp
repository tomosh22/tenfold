#include "model/basic_tenant.h"
#include "model/funcs_endpoint.h"
#include "model/packed_mailbox_policy.h"
#include "list/table158.h"
#include "model/types_notice.h"
#include "model/packed_catalog_tariff.h"
#include "model/types_invoice.h"
#include "list/macros_schedule.h"
#include "model/job.h"

namespace mx::model {


int /*@BasicTenant_1104_use3*/BasicTenant::scaleProduct(unsigned p0) const {
    double r = 0;
    r += mx::model::top(1, 1);
    r += mx::model::/*@encodeSession_1055_use3*/encodeSession(2.5);
    return (int)(r);
}

bool /*@BasicTenant_1104_use4*/BasicTenant::/*@shiftRule_1107_def0*/shiftRule(int p0, int p1) const {
    double r = 0;
    r += lis_table158_fn_5(1);
    r += mx::model::top(1, 1);
    r += mx::model::/*@markPayment_1054_use6*/markPayment(2.5);
    r += mx::model::id(3.5, 1);
    mx::model::Size228 a1 = 1;
    r += a1;
    mx::model::PackedCatalogTariff o2;
    r += o2.fetchMapper(7u);
    return (bool)(r);
}

long /*@BasicTenant_1104_use5*/BasicTenant::update() const {
    double r = 0;
    r += mx::model::top(1, 1);
    mx::model::/*@PackedMailboxPolicy_1050_use9*/PackedMailboxPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    r += mx::model::TiledInvoice_Count;
    r += this->/*@speed_1109_use0*/speed;
    r += lis_merge_165(1);
    mx::model::/*@Job_1081_use0*/Job o2;
    r += /*@Job_1081_var0*/o2.size();
    r += o2.score;
    return (long)(r);
}


}  // namespace mx::model
