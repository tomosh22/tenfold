#include "logging/funcs_index.h"
#include "logging/types_contract.h"
#include "base/funcs_index.h"
#include "mem/macros_order.h"
#include "util/fast_invoice.h"
#include "logging/macros_token.h"
#include "util/funcs_report.h"
#include "base/macros_notice.h"
#include "util/balance.h"
#include "util/basic_order.h"

namespace mx::logging {

long sweep_product(long p0) {
    double r = 0;
    r += p0;
    mx::logging::Handle115 a1 = 1;
    r += a1;
    r += bas_derive_index(1, 1);
    r += MEM_CUSTOMER_92;
    mx::util::FastInvoice d2;
    mx::util::FastInvoice* b3 = &d2;
    r += b3->count(1, 1);
    r += saveNotice121(1);
    mx::util::FastInvoice o4;
    r += o4.apply();
    r += o4.apply();
    r += o4.rank;
    return (long)(r);
}

long update_ticket(int p0, double p1) {
    double r = 0;
    r += mx::util::rotateTask(1);
    r += /*@BAS_JOB_15_43_use7*/BAS_JOB_15;
    mx::util::Balance d1;
    mx::util::BasicOrder* b2 = &d1;
    r += b2->count(1, 1);
    r += mx::logging::saveNotice121(1);
    return (long)(r);
}


}  // namespace mx::logging
