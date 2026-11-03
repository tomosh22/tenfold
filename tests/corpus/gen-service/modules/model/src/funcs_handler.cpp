#include "model/funcs_handler.h"
#include "model/address.h"
#include "domain/funcs_quota.h"
#include "domain/funcs_index.h"
#include "model/table146.h"
#include "domain/stable_event.h"
#include "domain/config.h"
#include "model/macros_token.h"
#include "model/types_repository.h"
#include "domain/nested_endpoint.h"
#include "domain/types_ledger.h"

namespace svc::model {

static int /*@helper_805_def0*/helper(int p0) { return p0 + 8; }

long visitEndpoint(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::model::/*@Address_771_use8*/Address o1;
    r += /*@Address_771_var5*/o1.selectTask(3.5, 1);
    r += o1.push(1);
    r += o1.push(1);
    svc::model::/*@Address_771_use9*/Address o2;
    r += o2.data(7u);
    r += svc::domain::apply(1, 1);
    r += svc::domain::flush(1);
    r += svc::model::table146_fn_1(1);
    return (long)(r);
}

unsigned collectQueue(unsigned p0) {
    double r = 0;
    r += p0;
    svc::domain::StableEvent o1;
    r += o1.compute_mapper(7u);
    r += o1.scale_repository(1, 1);
    svc::domain::/*@Config_543_use15*/Config<int> t2;
    r += t2.get();
    t2.set(3);
    r += MODEL_SCALE_150(2);
    svc::model::Count144 a3 = 1;
    r += a3;
    return (unsigned)(r);
}

unsigned visitIndex(unsigned p0) {
    double r = 0;
    r += p0;
    r += svc::domain::init(1, 1);
    svc::domain::NestedEndpoint o1;
    r += o1.collect_tariff(3.5, 1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    return (unsigned)(r);
}

long selectSession(int p0, double p1) {
    double r = 0;
    r += /*@helper_805_use0*/helper(1);
    r += svc::model::table146_fn_2(1);
    r += svc::domain::init(1, 1);
    r += svc::domain::kStep136;
    r += svc::model::table146_fn_1(1);
    return (long)(r);
}


}  // namespace svc::model
