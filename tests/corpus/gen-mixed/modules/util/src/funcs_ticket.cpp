#include "util/funcs_ticket.h"
#include "util/queue.h"
#include "util/macros_endpoint.h"
#include "util/funcs_endpoint.h"
#include "util/types_product.h"
#include "util/funcs_repository.h"
#include "base/types_cart.h"
#include "util/fast_invoice.h"
#include "util/token.h"
#include "util/funcs_job.h"

namespace mx {
namespace util {

double sweepIndex(long p0) {
    double r = 0;
    mx::util::Queue o1;
    r += o1.count(1, 1);
    r += o1.markIndex(7u);
    r += o1.apply();
    mx::util::Queue o2;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::util::Queue::update(1, 3.5);
    return (double)(r);
}

double name(long p0) {
    double r = 0;
    r += p0;
    mx::util::Queue o1;
    r += o1.run(2L);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += o1.next();
    r += /*@UTIL_SCHEDULE_40_164_use3*/UTIL_SCHEDULE_40;
    r += mx::util::count(1, 3.5);
    mx::util::Queue o2;
    r += o2.markIndex(7u);
    r += o2.empty(1, 3.5);
    r += mx::util::/*@kScale37_134_use8*/kScale37;
    r += mx::util::flushAccount();
    return (double)(r);
}

int save(long p0) {
    double r = 0;
    r += BAS_GREEN_9;
    r += mx::util::FastInvoice::shared_weight;
    return (int)(r);
}

double save(int p0) {
    double r = 0;
    mx::util::Queue o1;
    r += o1.run(2L);
    r += o1.count(1, 1);
    mx::util::Token o2;
    r += o2.offset;
    r += o2.offset;
    r += o2./*@rotateAudit_171_use4*/rotateAudit(1);
    r += mx::util::top(1);
    r += mx::util::data(1, 1);
    mx::util::Queue o3;
    r += o3.empty(1, 3.5);
    r += o3.apply();
    return (double)(r);
}


}  // namespace util
}  // namespace mx
