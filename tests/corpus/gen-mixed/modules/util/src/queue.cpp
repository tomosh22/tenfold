#include "util/queue.h"
#include "base/types_cart.h"
#include "util/funcs_repository.h"
#include "base/macros_notice.h"
#include "util/funcs_endpoint.h"
#include "util/types_product.h"
#include "util/funcs_job.h"

namespace mx {
namespace util {

static int pick(int p0) { return p0 + 5; }

double Queue::empty(int p0, double p1) {
    double r = 0;
    r += p0;
    r += BAS_MAX_2;
    r += empty(1, 3.5);
    r += mx::util::id(1, 1);
    return (double)(r);
}

unsigned Queue::deriveInvoice(int p0, double p1) {
    double r = 0;
    r += mx::util::data(2L);
    r += /*@BAS_JOB_15_43_use14*/BAS_JOB_15;
    r += mx::util::rotateGateway(1, 1);
    return (unsigned)(r);
}

int Queue::markIndex(unsigned p0) const {
    double r = 0;
    r += p0;
    r += mx::util::count(1, 3.5);
    r += bas_compute_19(1);
    r += mx::util::id(1, 1);
    r += mx::util::/*@kScale37_134_use9*/kScale37;
    r += mx::util::data(2L);
    r += /*@bas_step_11_12_use5*/bas_step_11;
    return (int)(r);
}

long Queue::apply() {
    double r = 0;
    r += mx::util::count(1, 3.5);
    r += run(2L);
    r += BAS_SCALE_16(2);
    r += mx::util::value();
    r += pick(1);
    return (long)(r);
}


}  // namespace util
}  // namespace mx
