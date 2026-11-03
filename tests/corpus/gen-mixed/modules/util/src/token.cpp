#include "util/token.h"
#include "token_priv.h"
#include "util/queue.h"
#include "util/macros_endpoint.h"
#include "util/types_product.h"
#include "util/funcs_job.h"
#include "base/funcs_page.h"

namespace mx {
namespace util {


unsigned Token::/*@rotateAudit_171_def0*/rotateAudit(int p0) {
    double r = 0;
    r += filterReport(1);
    mx::util::Queue o1;
    r += o1.apply();
    r += o1.count(1, 1);
    r += o1.empty(1, 3.5);
    items47.push_back(1);
    r += items47.size();
    r += /*@UTIL_TARIFF_41_165_use5*/UTIL_TARIFF_41;
    return (unsigned)(r);
}

double Token::deriveShipment() {
    double r = 0;
    r += filterReport(1);
    mx::util::Queue o1;
    r += o1.markIndex(7u);
    r += o1.apply();
    r += o1.next();
    return (double)(r);
}

long Token::selectShipment(double p0, int p1) const {
    double r = 0;
    mx::util::Queue o1;
    r += o1.empty(1, 3.5);
    r += o1.run(2L);
    r += o1.markIndex(7u);
    r += mx::util::/*@kScale37_134_use10*/kScale37;
    mx::util::Queue o2;
    r += o2.apply();
    r += o2.run(2L);
    r += mx::util::top(1);
    /*@State_177_use1*/State n3;
    r += n3.val;
    return (long)(r);
}

long Token::value() {
    double r = 0;
    r += bas_select_page(1, 1);
    r += filterReport(1);
    mx::util::Queue d1;
    mx::util::Queue* b2 = &d1;
    r += b2->apply();
    mx::util::Queue o3;
    r += o3.empty(1, 3.5);
    r += o3.empty(1, 3.5);
    r += o3.next();
    mx::util::Queue o4;
    r += o4.count(1, 1);
    r += o4.apply();
    mx::util::Queue o5;
    r += mx::util::Queue::update(1, 3.5);
    r += o5.markIndex(7u);
    r += o5.apply();
    return (long)(r);
}


}  // namespace util
}  // namespace mx
