#include "cache/funcs_invoice.h"
#include "cache/types_route.h"
#include "cache/types_invoice.h"
#include "config/types_tenant.h"
#include "cache/macros_task.h"
#include "cache/product2.h"
#include "config/tiled_tenant.h"
#include "util/types_mapper.h"

namespace svc::cache {

static int fold(int p0) { return p0 + 5; }

double updateEndpoint() {
    double r = 0;
    svc::cache::Size244 a1 = 1;
    r += a1;
    r += fold(1);
    return (double)(r);
}

int save(int p0, double p1) {
    double r = 0;
    r += svc::cache::kMargin255;
    r += static_cast<int>(svc::cache::NestedInvoiceEvent::/*@Auto_1254_use0*/Auto);
    return (int)(r);
}

long save(int p0) {
    double r = 0;
    r += svc::config::kStep26;
    r += svc::cache::loadInvoice251(1);
    return (long)(r);
}

unsigned set(int p0, double p1) {
    double r = 0;
    r += p1;
    svc::cache::Product2 o1;
    r += o1.name(1, 1);
    r += o1.name(1, 1);
    svc::config::TiledTenant o2;
    r += o2.flush(3.5, 1);
    r += o2.flush(3.5, 1);
    r += svc::config::TiledTenant::next(1);
    r += fold(1);
    svc::util::Handle11 a3 = 1;
    r += a3;
    return (unsigned)(r);
}


}  // namespace svc::cache
