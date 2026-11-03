#include "cache/helper.h"
#include "cache/macros_task.h"
#include "cache/macros_account.h"
#include "cache/types_invoice.h"
#include "config/funcs_invoice.h"
#include "cache/config.h"
#include "cache/node.h"

namespace svc::cache {

int Helper::shared_limit = 0;

unsigned Helper::data(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += svc::cache::loadInvoice251(1);
    r += CACHE_SHIPMENT_263;
    return (unsigned)(r);
}

bool Helper::measureShipment(double p0, int p1) {
    double r = 0;
    r += p1;
    svc::cache::NestedInvoiceEvent k1 = svc::cache::NestedInvoiceEvent::Last;
    r += static_cast<int>(k1);
    r += svc::cache::loadInvoice251(1);
    r += svc::config::bind_session(1, 1);
    svc::cache::Config o2;
    r += o2.name(7u);
    svc::cache::Node o3;
    r += o3.storeRoute(1, 1);
    r += o3.clear(1, 3.5);
    r += o3.clear(1, 3.5);
    r += measureShipment(3.5, 1);
    return (bool)(r);
}


}  // namespace svc::cache
