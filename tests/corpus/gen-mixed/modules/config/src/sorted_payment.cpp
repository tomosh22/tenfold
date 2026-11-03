#include "config/sorted_payment.h"
#include "util/funcs_report.h"
#include "config/masked_shipment.h"
#include "config/primary_mapper.h"
#include "config/types_cart.h"
#include "config/local_handler_mapper.h"
#include "config/funcs_index_58.h"
#include "util/funcs_job.h"
#include "util/types_product.h"
#include "config/funcs_index.h"
#include "util/types_mailbox.h"
#include "util/macros_endpoint.h"
#include "util/node.h"
#include "util/basic_order.h"
#include "util/queue.h"

namespace mx {
namespace config {


int /*@SortedPayment_342_use3*/SortedPayment::update_validator(long p0) {
    double r = 0;
    r += p0;
    r += mx::util::run(1);
    mx::config::/*@MaskedShipment_277_use15*/MaskedShipment o1;
    r += /*@MaskedShipment_277_var1*/o1.mass;
    r += o1.measure_tariff(1);
    mx::config::PrimaryMapper o2;
    r += o2.select_repository(1);
    r += mx::config::/*@kScale56_255_use2*/kScale56;
    mx::config::LocalHandlerMapper d3;
    mx::config::LocalHandlerMapper* b4 = &d3;
    r += b4->select_repository(1);
    r += mx::config::BasicSession_First;
    return (int)(r);
}

long /*@SortedPayment_342_use4*/SortedPayment::add() const {
    double r = 0;
    r += mx::config::derive_ticket(2L);
    r += mx::util::top(1);
    return (long)(r);
}

double /*@SortedPayment_342_use5*/SortedPayment::name(int p0) {
    double r = 0;
    r += p0;
    r += shift_gateway(7u);
    r += mx::util::/*@kScale37_134_use2*/kScale37;
    r += size(2L);
    mx::config::PrimaryMapper o1;
    r += o1.select_repository(1);
    r += mx::config::PrimaryMapper::/*@reset_contract_261_use3*/reset_contract(7u);
    r += o1.select_repository(1);
    r += mx::config::verify_schedule(3.5, 1);
    return (double)(r);
}

double /*@SortedPayment_342_use6*/SortedPayment::build_rule(double p0) {
    double r = 0;
    r += p0;
    r += static_cast<int>(mx::util::/*@RadialEndpoint_220_use0*/RadialEndpoint::Red);
    r += mx::config::verify_schedule(3.5, 1);
    r += /*@UTIL_SCHEDULE_40_164_use2*/UTIL_SCHEDULE_40;
    mx::util::Node d1;
    mx::util::BasicOrder* b2 = &d1;
    r += b2->apply();
    return (double)(r);
}

long /*@SortedPayment_342_use7*/SortedPayment::shift_gateway(unsigned p0) {
    double r = 0;
    r += mx::util::kCap51;
    mx::util::Queue o1;
    r += o1.markIndex(7u);
    r += mx::util::Queue::update(1, 3.5);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    mx::util::Queue d2;
    mx::util::BasicOrder* b3 = &d2;
    r += b3->empty(1, 3.5);
    mx::util::Queue o4;
    r += o4.markIndex(7u);
    r += o4.apply();
    r += mx::config::verify_schedule(3.5, 1);
    return (long)(r);
}


}  // namespace config
}  // namespace mx
