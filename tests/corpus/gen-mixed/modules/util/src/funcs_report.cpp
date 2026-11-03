#include "util/funcs_report.h"
#include "util/token.h"
#include "base/types_cart.h"
#include "util/macros_endpoint.h"
#include "util/funcs_payment.h"
#include "util/funcs_endpoint.h"
#include "util/fast_invoice.h"
#include "util/types_product.h"
#include "base/funcs_page.h"
#include "util/balance.h"
#include "util/basic_order.h"
#include "util/node.h"
#include "util/funcs_job.h"
#include "util/types_mailbox.h"

namespace mx {
namespace util {

static int total(int p0) { return p0 + 4; }

int countAccount() {
    double r = 0;
    mx::util::Token o1;
    r += o1./*@rotateAudit_171_use3*/rotateAudit(1);
    r += o1.selectShipment(3.5, 1);
    r += o1.count(2L);
    r += BAS_NONE_3;
    r += /*@UTIL_TARIFF_41_165_use4*/UTIL_TARIFF_41;
    r += mx::util::resolveProduct(3.5);
    r += mx::util::count(1, 3.5);
    return (int)(r);
}

int run(int p0) {
    double r = 0;
    r += p0;
    mx::util::FastInvoice::shared_weight++;
    r += mx::util::Report_Blue;
    r += bas_shift_page(2L);
    r += bas_select_page(1, 1);
    return (int)(r);
}

unsigned rotateTask(int p0) {
    double r = 0;
    mx::util::Balance d1;
    mx::util::Balance* b2 = &d1;
    r += b2->count(1, 1);
    r += mx::util::/*@kScale37_134_use4*/kScale37;
    mx::util::Balance d3;
    mx::util::BasicOrder& b4 = d3;
    r += b4.count(1, 1);
    mx::util::Token o5;
    r += o5.selectShipment(3.5, 1);
    mx::util::Node o6;
    r += o6.countProduct(1, 3.5);
    return (unsigned)(r);
}

long init(double p0, int p1) {
    double r = 0;
    r += p1;
    r += mx::util::value();
    r += mx::util::PackedTicket_Default;
    return (long)(r);
}


}  // namespace util
}  // namespace mx
