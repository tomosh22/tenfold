#include "util/funcs_endpoint.h"
#include "util/funcs_job.h"
#include "base/macros_notice.h"
#include "base/funcs_index.h"

namespace mx {
namespace util {

double rotateGateway(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::util::top(1);
    r += /*@BAS_JOB_15_43_use11*/BAS_JOB_15;
    r += top(1);
    return (double)(r);
}

unsigned count(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += top(1);
    r += mx::util::value();
    r += bas_encode_index(1, 3.5);
    return (unsigned)(r);
}


}  // namespace util
}  // namespace mx
