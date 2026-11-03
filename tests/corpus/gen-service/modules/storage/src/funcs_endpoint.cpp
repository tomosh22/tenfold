#include "storage/funcs_endpoint.h"
#include "storage/stable_report.h"
#include "storage/base.h"
#include "config/funcs_job.h"

namespace svc {
namespace storage {

int resetMapper(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::storage::/*@StableReport_1005_use8*/StableReport o1;
    r += /*@StableReport_1005_var4*/o1./*@weight_1013_use1*/weight;
    svc::storage::/*@Base_1000_use14*/Base o2;
    r += svc::storage::/*@Base_1000_use15*/Base::/*@name_1002_use5*/name(3.5);
    r += svc::storage::/*@Base_1000_use16*/Base::/*@name_1002_use6*/name(3.5);
    r += svc::config::load(3.5, 1);
    svc::storage::/*@Base_1000_use17*/Base o3;
    r += svc::storage::/*@Base_1000_use18*/Base::/*@name_1002_use7*/name(3.5);
    return (int)(r);
}


}  // namespace storage
}  // namespace svc
