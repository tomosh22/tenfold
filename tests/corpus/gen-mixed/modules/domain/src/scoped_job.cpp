#include "domain/scoped_job.h"
#include "scoped_job_priv.h"
#include "domain/funcs_address.h"
#include "domain/types_index.h"
#include "base/macros_notice.h"
#include "config/local_handler_mapper.h"
#include "config/primary_mapper.h"
#include "config/registry.h"
#include "config/masked_shipment.h"
#include "config/sorted_payment.h"

namespace mx::domain {

int ScopedJob::/*@shared_weight_684_def0*/shared_weight = 0;

unsigned ScopedJob::splitToken(int p0, int p1) {
    double r = 0;
    r += p0;
    r += mx::domain::scaleQueue(7u);
    mx::domain::LazyLedger k1 = mx::domain::LazyLedger::Default;
    r += static_cast<int>(k1);
    r += bas_compute_19(1);
    mx::config::LocalHandlerMapper d2;
    mx::config::PrimaryMapper& b3 = d2;
    r += b3.split_ledger();
    r += bas_compute_19(1);
    return (unsigned)(r);
}

int ScopedJob::count(int p0) const {
    double r = 0;
    r += p0;
    r += mx::config::Registry::shared_width;
    mx::config::/*@MaskedShipment_277_use19*/MaskedShipment o1;
    r += /*@MaskedShipment_277_var4*/o1.stamp;
    r += o1.shift_task(3.5, 1);
    r += o1.measure_tariff(1);
    return (int)(r);
}

double ScopedJob::init(int p0) const {
    double r = 0;
    r += p0;
    r += mx::domain::/*@QueuedQueue_Unknown_656_use0*/QueuedQueue_Unknown;
    mx::config::/*@SortedPayment_342_use11*/SortedPayment o1;
    r += /*@SortedPayment_342_var2*/o1.name(1);
    r += mx::domain::scaleQueue(7u);
    mx::config::Registry o2;
    r += o2.pop(7u);
    r += o2.empty(1);
    r += /*@flushPolicy_687_use0*/flushPolicy(1);
    return (double)(r);
}


}  // namespace mx::domain
