#include "config/local_handler_mapper.h"
#include "util/types_product.h"
#include "base/macros_notice.h"
#include "util/fast_invoice.h"
#include "util/macros_endpoint.h"
#include "config/funcs_index_58.h"
#include "config/funcs_index.h"
#include "util/queue.h"
#include "config/types_cart.h"
#include "config/masked_shipment.h"

namespace mx {
namespace config {

int LocalHandlerMapper::shared_count = 0;

unsigned LocalHandlerMapper::select_repository(int p0) const {
    double r = 0;
    r += mx::util::/*@kScale37_134_use1*/kScale37;
    r += BAS_SCALE_16(2);
    mx::util::FastInvoice d1;
    mx::util::FastInvoice& b2 = d1;
    r += b2.count(1, 1);
    return (unsigned)(r);
}

long LocalHandlerMapper::/*@reset_318_def0*/reset(int p0) const {
    double r = 0;
    r += fetch_queue(1, 1);
    r += /*@UTIL_TARIFF_41_165_use2*/UTIL_TARIFF_41;
    r += mx::config::add();
    r += mx::config::save();
    r += reset(2L);
    r += /*@reset_318_use1*/reset(1);
    return (long)(r);
}

long LocalHandlerMapper::reset(long p0) const {
    double r = 0;
    r += p0;
    r += UTIL_SCALE_43(2);
    r += /*@reset_318_use2*/reset(1);
    mx::util::Queue o1;
    r += o1.apply();
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += o1.run(2L);
    r += flags;
    r += mx::config::kBase55;
    return (long)(r);
}

int LocalHandlerMapper::fetch_queue(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += mx::config::/*@MaskedShipment_277_use8*/MaskedShipment::/*@shared_speed_286_use4*/shared_speed;
    r += select_repository(1);
    return (int)(r);
}


}  // namespace config
}  // namespace mx
