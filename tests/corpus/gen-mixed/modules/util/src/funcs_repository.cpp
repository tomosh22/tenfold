#include "util/funcs_repository.h"
#include "base/macros_notice.h"
#include "base/types_cart.h"
#include "base/funcs_region.h"
#include "util/funcs_endpoint.h"
#include "util/funcs_job.h"
#include "util/types_product.h"
#include "base/funcs_page.h"
#include "base/types_report.h"

namespace mx {
namespace util {

long id(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += /*@BAS_JOB_15_43_use12*/BAS_JOB_15;
    r += BAS_COUNT_1;
    return (long)(r);
}

unsigned id(int p0) {
    double r = 0;
    r += bas_measure_region(1);
    r += mx::util::rotateGateway(1, 1);
    r += mx::util::value();
    mx::util::/*@Size38_135_use1*/Size38 a1 = 1;
    r += a1;
    r += BAS_SCALE_17(2);
    return (unsigned)(r);
}

double flushEndpoint(long p0) {
    double r = 0;
    r += p0;
    r += mx::util::value();
    r += bas_measure_region(1);
    r += mx::util::top(1);
    r += top(1);
    mx::util::/*@Size38_135_use2*/Size38 a1 = 1;
    r += a1;
    r += mx::util::/*@kScale37_134_use5*/kScale37;
    return (double)(r);
}

bool data(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::util::top(1);
    r += BAS_SCALE_16(2);
    r += mx::util::count(1, 3.5);
    return (bool)(r);
}

unsigned data() {
    double r = 0;
    r += mx::util::top(1);
    r += top(1);
    r += count(1, 3.5);
    r += mx::util::/*@kScale37_134_use6*/kScale37;
    r += mx::util::/*@kScale37_134_use7*/kScale37;
    r += bas_compute_region(1, 3.5);
    return (unsigned)(r);
}

int data(long p0) {
    double r = 0;
    r += bas_flush_page(1, 3.5);
    r += mx::util::top(1);
    r += mx::util::top(1);
    return (int)(r);
}

bool flushAccount(int p0) {
    double r = 0;
    r += mx::util::Catalog_Green;
    r += /*@BAS_JOB_15_43_use13*/BAS_JOB_15;
    r += top(1);
    mx::util::/*@Size38_135_use3*/Size38 a1 = 1;
    r += a1;
    r += BAS_LAST_25;
    return (bool)(r);
}


}  // namespace util
}  // namespace mx
