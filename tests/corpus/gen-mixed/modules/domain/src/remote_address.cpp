#include "domain/remote_address.h"
#include "domain/funcs_address.h"
#include "config/funcs_mailbox.h"
#include "domain/scoped_job.h"

namespace mx::domain {

static int pick(int p0) { return p0 + 1; }

int /*@RemoteAddress_688_use11*/RemoteAddress::/*@combineTask_689_def0*/combineTask(double p0) {
    double r = 0;
    r += mx::domain::flushRule(1, 1);
    r += mx::config::find_audit(3.5);
    return (int)(r);
}

long /*@RemoteAddress_688_use12*/RemoteAddress::top(long p0) {
    double r = 0;
    mx::domain::ScopedJob o1;
    r += o1.init(1);
    r += o1.splitToken(1, 1);
    r += o1.splitToken(1, 1);
    r += shift_task(3.5, 1);
    return (long)(r);
}


}  // namespace mx::domain
