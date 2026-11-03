#include "util/funcs_order.h"
#include "util/funcs_shipment.h"
#include "util/provider.h"
#include "util/buffer.h"
#include "util/static_ledger.h"
#include "util/types_profile.h"
#include "util/funcs_product.h"
#include "util/types_mapper.h"
#include "util/profile.h"
#include "util/types_profile_13.h"
#include "util/config.h"
#include "util/types_catalog.h"

namespace svc::util {

int /*@visitTenant_104_def0*/visitTenant(double p0) {
    double r = 0;
    r += p0;
    r += svc::util::measureToken(2L);
    svc::util::/*@Provider_67_use9*/Provider d1;
    svc::util::Buffer& b2 = d1;
    r += b2./*@combineCustomer_27_use7*/combineCustomer(1, 1);
    r += svc::util::splitQueue(7u);
    svc::util::Buffer d3;
    svc::util::StaticLedger& b4 = d3;
    r += b4.combineCustomer(1, 1);
    r += svc::util::kScale1;
    return (int)(r);
}

bool resolveQueue(double p0, int p1) {
    double r = 0;
    r += encodePayment();
    r += svc::util::kBase9;
    r += svc::util::encodePayment();
    return (bool)(r);
}

int resolveQueue(long p0) {
    double r = 0;
    r += p0;
    r += svc::util::kScale1;
    r += svc::util::measureToken(1);
    svc::util::/*@Provider_67_use10*/Provider o1;
    r += /*@Provider_67_var3*/o1./*@combineCustomer_70_use2*/combineCustomer(1, 1);
    r += o1./*@combineCustomer_70_use3*/combineCustomer(1, 1);
    r += svc::util::splitQueue(7u);
    r += svc::util::measureToken(1);
    svc::util::/*@StaticCatalog_0_use12*/StaticCatalog k2 = svc::util::/*@StaticCatalog_0_use13*/StaticCatalog::Last;
    r += static_cast<int>(k2);
    return (int)(r);
}

int scaleTicket(double p0, int p1) {
    double r = 0;
    r += p1;
    svc::util::Profile d1;
    svc::util::Profile* b2 = &d1;
    r += b2->combineShipment(3.5);
    svc::util::Buffer o3;
    r += o3.apply();
    return (int)(r);
}

double scaleTicket() {
    double r = 0;
    svc::util::/*@Provider_67_use11*/Provider d1;
    svc::util::/*@Provider_67_use12*/Provider* b2 = &d1;
    r += b2->combineShipment(3.5);
    r += svc::util::kLimit14;
    r += svc::util::Buffer::/*@shared_ratio_33_use17*/shared_ratio;
    svc::util::Config o3;
    r += o3.splitContract(3.5, 1);
    r += measureToken(2L);
    svc::util::Profile o4;
    r += svc::util::Profile::storeAudit(1);
    return (double)(r);
}

bool filterBalance(int p0) {
    double r = 0;
    r += p0;
    svc::util::Config o1;
    r += o1.width;
    r += svc::util::measureToken(2L);
    r += svc::util::Buffer::/*@shared_ratio_33_use18*/shared_ratio;
    r += svc::util::splitQueue(7u);
    svc::util::Profile d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    svc::util::Config o4;
    r += o4.splitContract(3.5, 1);
    r += o4.loadAddress(1);
    return (bool)(r);
}

long filterBalance(double p0) {
    double r = 0;
    r += p0;
    r += svc::util::combineMailbox(3.5);
    r += svc::util::kBase5;
    return (long)(r);
}

int bindProfile(unsigned p0) {
    double r = 0;
    svc::util::StaticLedger o1;
    r += o1.combineCustomer(1, 1);
    r += o1.speed;
    r += o1.verifyAccount(1, 1);
    r += svc::util::Customer_Auto;
    svc::util::Profile o2;
    r += o2./*@combineCustomer_89_use2*/combineCustomer(1, 1);
    svc::util::Config o3;
    r += o3.loadAddress(1);
    r += o3.mergeCart(2L);
    r += svc::util::/*@kCap15_101_use0*/kCap15;
    svc::util::Config o4;
    r += o4.mergeCart(2L);
    r += o4.splitContract(3.5, 1);
    return (int)(r);
}


}  // namespace svc::util
