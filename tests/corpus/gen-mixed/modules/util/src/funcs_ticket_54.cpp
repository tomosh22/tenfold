#include "util/funcs_ticket_54.h"
#include "util/queue.h"
#include "util/basic_order.h"
#include "util/fast_invoice.h"
#include "util/types_product.h"
#include "base/funcs_page.h"
#include "util/types_mailbox.h"
#include "util/funcs_repository.h"
#include "util/funcs_job.h"
#include "util/token.h"
#include "util/funcs_payment.h"
#include "util/macros_endpoint.h"
#include "util/balance.h"
#include "util/funcs_ticket.h"
#include "util/funcs_endpoint.h"

namespace mx {
namespace util {

double make(long p0) {
    double r = 0;
    mx::util::Queue d1;
    mx::util::BasicOrder& b2 = d1;
    r += b2.count(1, 1);
    r += mx::util::FastInvoice::shared_weight;
    return (double)(r);
}

long make(double p0, int p1) {
    double r = 0;
    mx::util::/*@Size38_135_use4*/Size38 a1 = 1;
    r += a1;
    r += bas_flush_page(1, 3.5);
    mx::util::/*@RadialEndpoint_220_use1*/RadialEndpoint k2 = mx::util::/*@RadialEndpoint_220_use2*/RadialEndpoint::Red;
    r += static_cast<int>(k2);
    r += data();
    mx::util::FastInvoice o3;
    r += o3.index;
    r += o3.apply();
    r += o3.probeIndex(2L);
    return (long)(r);
}

int collectCustomer(double p0, int p1) {
    double r = 0;
    r += p1;
    r += top(1);
    mx::util::FastInvoice d1;
    mx::util::FastInvoice* b2 = &d1;
    r += b2->empty(1, 3.5);
    r += mx::util::data();
    mx::util::Token o3;
    r += o3.selectShipment(3.5, 1);
    r += mx::util::Token::deriveShipment();
    r += o3.selectShipment(3.5, 1);
    mx::util::Queue o4;
    r += o4.empty(1, 3.5);
    mx::util::Queue o5;
    r += o5.run(2L);
    r += mx::util::Queue::update(1, 3.5);
    return (int)(r);
}

unsigned load(int p0, double p1) {
    double r = 0;
    r += p1;
    r += mx::util::/*@scaleToken_199_use0*/scaleToken(1);
    r += mx::util::FastInvoice::shared_weight;
    r += mx::util::value();
    r += mx::util::PackedTicket_Auto;
    return (unsigned)(r);
}

unsigned rotateToken(int p0, int p1) {
    double r = 0;
    mx::util::Queue d1;
    mx::util::Queue& b2 = d1;
    r += b2.markIndex(7u);
    r += static_cast<int>(mx::util::/*@RadialEndpoint_220_use3*/RadialEndpoint::Blue);
    r += /*@UTIL_SCHEDULE_40_164_use4*/UTIL_SCHEDULE_40;
    return (unsigned)(r);
}

long rotateToken(int p0, double p1) {
    double r = 0;
    mx::util::Token o1;
    r += o1.offset;
    r += mx::util::Token::value();
    r += mx::util::Token::value();
    r += data(1, 1);
    mx::util::Balance d2;
    mx::util::BasicOrder& b3 = d2;
    r += b3.count(1, 1);
    r += name(2L);
    r += find(1);
    r += rotateGateway(1, 1);
    return (long)(r);
}


}  // namespace util
}  // namespace mx
