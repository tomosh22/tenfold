#include "config/tiled_tenant.h"
#include "util/types_mapper.h"
#include "config/table51.h"
#include "config/types_contract.h"
#include "config/active_audit.h"
#include "config/funcs_job.h"

namespace svc {
namespace config {


unsigned TiledTenant::sweep_endpoint(int p0, double p1) {
    double r = 0;
    r += svc::util::kBase8;
    r += svc::config::table51_fn_2(1);
    r += svc::config::kStep40;
    return (unsigned)(r);
}

unsigned TiledTenant::flush(double p0, int p1) const {
    double r = 0;
    r += p1;
    r += flush(3.5, 1);
    svc::config::ActiveAudit o1;
    r += o1.run(2L);
    r += o1.run(2L);
    svc::config::ActiveAudit o2;
    r += o2.add();
    r += o2.add();
    r += o2.split_payment(3.5, 1);
    return (unsigned)(r);
}

long TiledTenant::next(int p0) {
    double r = 0;
    r += p0;
    r += next(1);
    r += svc::config::load(2L);
    return (long)(r);
}


}  // namespace config
}  // namespace svc
