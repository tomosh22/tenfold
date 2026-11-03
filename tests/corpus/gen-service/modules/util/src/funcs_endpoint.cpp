#include "util/funcs_endpoint.h"
#include "util/funcs_order.h"
#include "util/buffer.h"
#include "util/funcs_shipment.h"
#include "util/static_ledger.h"
#include "util/types_profile.h"
#include "util/funcs_account.h"
#include "util/binned_gateway_job.h"

namespace svc::util {

static int /*@norm_136_def0*/norm(int p0) { return p0 + 2; }

unsigned name(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::util::filterBalance(1);
    svc::util::Buffer d1;
    svc::util::Buffer& b2 = d1;
    r += b2./*@combineCustomer_27_use5*/combineCustomer(1, 1);
    r += svc::util::Buffer::/*@shared_ratio_33_use16*/shared_ratio;
    r += svc::util::measureToken(1);
    return (unsigned)(r);
}

long name() {
    double r = 0;
    r += bindProfile(7u);
    svc::util::Buffer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += splitQueue(7u);
    svc::util::StaticLedger o3;
    r += o3.speed;
    r += o3.score;
    r += o3.flushBalance(1, 3.5);
    r += svc::util::splitQueue(7u);
    return (long)(r);
}

int id(int p0, double p1) {
    double r = 0;
    svc::util::/*@StaticCatalog_0_use10*/StaticCatalog k1 = svc::util::/*@StaticCatalog_0_use11*/StaticCatalog::First;
    r += static_cast<int>(k1);
    r += svc::util::bindProfile(7u);
    r += svc::util::update(2L);
    r += measureToken(1);
    svc::util::Buffer d2;
    svc::util::StaticLedger* b3 = &d2;
    r += b3->combineCustomer(1, 1);
    return (int)(r);
}

int find(int p0) {
    double r = 0;
    r += p0;
    r += svc::util::kScale1;
    svc::util::Buffer o1;
    r += o1./*@combineCustomer_27_use6*/combineCustomer(1, 1);
    r += o1.scale;
    svc::util::Handle2 a2 = 1;
    r += a2;
    svc::util::BinnedGatewayJob d3;
    svc::util::StaticLedger& b4 = d3;
    r += b4.combineCustomer(1, 1);
    r += svc::util::markAudit();
    r += /*@norm_136_use0*/norm(1);
    return (int)(r);
}


}  // namespace svc::util
