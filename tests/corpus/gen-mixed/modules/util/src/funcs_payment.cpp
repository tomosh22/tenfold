#include "util/funcs_payment.h"
#include "util/token.h"
#include "util/funcs_job.h"
#include "util/macros_endpoint.h"
#include "util/fast_invoice.h"
#include "util/basic_order.h"
#include "util/funcs_repository.h"
#include "base/funcs_index.h"
#include "util/funcs_endpoint.h"
#include "util/queue.h"
#include "util/types_product.h"

namespace mx {
namespace util {

bool find(int p0) {
    double r = 0;
    r += p0;
    mx::util::Token o1;
    r += o1.offset;
    r += o1./*@rotateAudit_171_use2*/rotateAudit(1);
    r += top(1);
    return (bool)(r);
}

long clear(int p0, int p1) {
    double r = 0;
    r += p1;
    r += mx::util::/*@bindBalance45_169_use0*/bindBalance45(1);
    mx::util::FastInvoice o1;
    r += o1.apply();
    r += o1.markIndex(7u);
    mx::util::FastInvoice d2;
    mx::util::BasicOrder& b3 = d2;
    r += b3.empty(1, 3.5);
    mx::util::Token o4;
    r += o4.count(2L);
    r += o4.selectShipment(3.5, 1);
    r += mx::util::Token::value();
    r += mx::util::data(2L);
    return (long)(r);
}

double flush(int p0, int p1) {
    double r = 0;
    r += p0;
    r += bas_encode_index(1, 3.5);
    r += mx::util::count(1, 3.5);
    r += mx::util::data(2L);
    return (double)(r);
}

double resolveProduct(double p0) {
    double r = 0;
    r += p0;
    mx::util::FastInvoice d1;
    mx::util::FastInvoice& b2 = d1;
    r += b2.markIndex(7u);
    mx::util::Queue d3;
    mx::util::BasicOrder* b4 = &d3;
    r += b4->count(1, 1);
    mx::util::FastInvoice d5;
    mx::util::FastInvoice& b6 = d5;
    r += b6.empty(1, 3.5);
    mx::util::FastInvoice o7;
    r += o7.index;
    r += o7./*@flush_186_use0*/flush(1);
    r += o7.markIndex(7u);
    r += UTIL_SCALE_43(2);
    mx::util::Queue o8;
    r += o8.count(1, 1);
    r += o8.next();
    r += mx::util::Queue::update(1, 3.5);
    return (double)(r);
}

double resolveProduct(double p0, int p1) {
    double r = 0;
    r += p1;
    r += mx::util::Catalog_Last;
    r += mx::util::value();
    return (double)(r);
}


}  // namespace util
}  // namespace mx
