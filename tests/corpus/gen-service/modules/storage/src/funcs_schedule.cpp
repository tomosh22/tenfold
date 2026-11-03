#include "storage/funcs_schedule.h"
#include "util/buffer.h"
#include "storage/node.h"
#include "storage/contract.h"
#include "storage/funcs_address.h"
#include "util/config.h"
#include "storage/funcs_session.h"
#include "config/table51.h"
#include "storage/funcs_endpoint.h"

namespace svc {
namespace storage {

static int step(int p0) { return p0 + 1; }

double count(int p0, double p1) {
    double r = 0;
    r += p1;
    r += svc::util::Buffer::/*@shared_ratio_33_use11*/shared_ratio;
    svc::storage::/*@Node_1077_use4*/Node o1;
    r += /*@Node_1077_var1*/o1.resetCatalog(3.5);
    r += o1.verifyPayment(7u);
    r += o1./*@combineCustomer_1084_use1*/combineCustomer(1, 1);
    svc::storage::/*@Contract_1055_use3*/Contract o2;
    r += /*@Contract_1055_var0*/o2.storeHandler(1, 1);
    r += o2.storeHandler(1, 1);
    return (double)(r);
}

unsigned reset() {
    double r = 0;
    r += svc::storage::sweepJob(3.5, 1);
    svc::util::Config o1;
    r += o1./*@find_9_use3*/find(7u);
    r += o1.splitContract(3.5, 1);
    r += filterInvoice(1, 3.5);
    return (unsigned)(r);
}

unsigned findContract238(double p0) {
    double r = 0;
    r += svc::config::table51_fn_2(1);
    r += svc::storage::combineLedger(1);
    return (unsigned)(r);
}


}  // namespace storage
}  // namespace svc
