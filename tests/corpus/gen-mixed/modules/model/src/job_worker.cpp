#include "model/job_worker.h"
#include "model/packed_mailbox_policy.h"
#include "model/macros_profile.h"
#include "model/types_invoice.h"
#include "model/job.h"

namespace mx::model {


double JobWorker::encodeTask252(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += mx::model::/*@markPayment_1054_use9*/markPayment<int>(2);
    r += MODEL_SCALE_246(2);
    r += mx::model::/*@TiledInvoice_Max_977_use1*/TiledInvoice_Max;
    r += encodeTask252(1, 3.5);
    mx::model::/*@Job_1081_use4*/Job o1;
    r += /*@Job_1081_var2*/o1.total;
    return (double)(r);
}


}  // namespace mx::model
