#include "domain/funcs_index.h"
#include "util/types_profile.h"
#include "logging/radial_index_repository.h"
#include "logging/funcs_balance.h"
#include "domain/config.h"
#include "util/funcs_product.h"
#include "util/funcs_shipment.h"
#include "logging/notice.h"
#include "domain/types_mailbox.h"
#include "logging/info.h"

namespace svc::domain {

unsigned flush(int p0) {
    double r = 0;
    r += p0;
    r += svc::util::kScale1;
    svc::logging::RadialIndexRepository o1;
    r += o1.mass;
    r += o1.mass;
    r += svc::logging::flush(1);
    r += svc::domain::/*@sweep_queue_547_use1*/sweep_queue<int>(2);
    return (unsigned)(r);
}

unsigned build_mapper(int p0, int p1) {
    double r = 0;
    r += p0;
    r += svc::util::sortValidator(2L);
    r += svc::logging::RadialIndexRepository::shared_height;
    r += svc::util::measureToken(2L);
    svc::logging::Notice o1;
    r += o1.sweep_rule();
    r += o1.sweep_rule();
    svc::domain::/*@StableToken_535_use2*/StableToken k2 = svc::domain::/*@StableToken_535_use3*/StableToken::Auto;
    r += static_cast<int>(k2);
    svc::logging::Info o3;
    r += o3./*@score_446_use0*/score;
    return (unsigned)(r);
}


}  // namespace svc::domain
