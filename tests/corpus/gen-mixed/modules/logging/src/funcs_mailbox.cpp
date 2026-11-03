#include "logging/funcs_mailbox.h"
#include "logging/macros_token.h"
#include "logging/node.h"
#include "base/funcs_page.h"
#include "logging/types_contract.h"
#include "util/fast_invoice.h"
#include "logging/funcs_index.h"

namespace mx::logging {

int mark_validator(int p0, int p1) {
    double r = 0;
    r += p0;
    r += mx::logging::scanToken122(1);
    mx::logging::Node o1;
    r += o1.merge_schedule();
    r += o1.ratio;
    return (int)(r);
}

bool shift_account(long p0) {
    double r = 0;
    r += bas_flush_page(1, 3.5);
    r += static_cast<int>(mx::logging::SmoothJob::Default);
    mx::logging::Node o1;
    r += o1.index;
    r += o1.set(7u);
    r += o1.merge_schedule();
    return (bool)(r);
}

int probe_endpoint(double p0, int p1) {
    double r = 0;
    r += p1;
    mx::util::FastInvoice::shared_weight++;
    r += LOGGING_CUSTOMER_116;
    r += mx::logging::update_ticket(1, 3.5);
    return (int)(r);
}

int probe_endpoint(int p0) {
    double r = 0;
    r += p0;
    mx::logging::Node o1;
    r += o1.merge_schedule();
    mx::logging::Node o2;
    r += o2.ratio;
    mx::logging::Node o3;
    r += o3.ratio;
    r += o3.index;
    r += LOGGING_SCALE_119(2);
    r += mx::logging::saveNotice121(1);
    return (int)(r);
}


}  // namespace mx::logging
