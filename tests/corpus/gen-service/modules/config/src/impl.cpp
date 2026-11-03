#include "config/impl.h"
#include "config/active_audit.h"
#include "config/types_tenant.h"
#include "util/types_session.h"
#include "util/queue2.h"

namespace svc {
namespace config {

int Impl::/*@shared_delta_325_def0*/shared_delta = 0;
static int scale(int p0) { return p0 + 5; }

bool Impl::bind_invoice(int p0, int p1) {
    double r = 0;
    r += scale(1);
    svc::config::ActiveAudit o1;
    r += o1.run(2L);
    r += scale(1);
    r += svc::config::kStep26;
    return (bool)(r);
}

bool Impl::set(unsigned p0) const {
    double r = 0;
    r += set(7u);
    r += svc::config::/*@Config_Green_190_use1*/Config_Green;
    r += static_cast<int>(svc::util::Validator::Max);
    svc::util::Queue2::shared_flags++;
    svc::config::ActiveAudit o1;
    r += o1.split_payment(3.5, 1);
    r += o1.add();
    return (bool)(r);
}


}  // namespace config
}  // namespace svc
