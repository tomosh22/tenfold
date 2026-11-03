#include "domain/funcs_queue.h"
#include "domain/funcs_index.h"
#include "domain/types_mailbox.h"
#include "domain/funcs_ticket.h"
#include "domain/config.h"
#include "logging/info.h"

namespace svc::domain {

unsigned scale_address(long p0) {
    double r = 0;
    r += p0;
    r += svc::domain::build_mapper(1, 1);
    r += static_cast<int>(svc::domain::/*@StableToken_535_use4*/StableToken::Red);
    r += svc::domain::scale_route(2L);
    return (unsigned)(r);
}

bool save(int p0) {
    double r = 0;
    r += p0;
    r += init(1, 1);
    r += svc::domain::build_mapper(1, 1);
    svc::domain::/*@Config_543_use8*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += /*@get_551_use0*/get(2L);
    svc::logging::Info o2;
    r += o2./*@score_446_use1*/score;
    return (bool)(r);
}


}  // namespace svc::domain
