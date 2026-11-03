#include "storage/base.h"
#include "base_priv.h"
#include "util/binned_gateway_job.h"
#include "config/active_audit.h"
#include "storage/types_payment.h"

namespace svc {
namespace storage {


long /*@Base_1000_use9*/Base::/*@name_1002_def0*/name(double p0) {
    double r = 0;
    r += p0;
    svc::util::BinnedGatewayJob o1;
    r += o1.set(3.5);
    r += o1.set(3.5);
    r += o1.mergeValidator(1);
    r += /*@name_1002_use1*/name(3.5);
    svc::config::ActiveAudit o2;
    r += o2.split_payment(3.5, 1);
    r += o2.split_payment(1, 1);
    r += o2.count_task(1);
    r += svc::storage::Payment_Green;
    svc::config::ActiveAudit o3;
    r += o3.count_task(1);
    r += /*@name_1002_use2*/name(3.5);
    return (long)(r);
}


}  // namespace storage
}  // namespace svc
