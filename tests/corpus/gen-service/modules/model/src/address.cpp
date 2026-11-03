#include "model/address.h"
#include "model/types_repository.h"
#include "domain/stable_event.h"
#include "domain/queue_handler.h"
#include "model/macros_token.h"
#include "domain/nested_endpoint.h"
#include "domain/funcs_report.h"
#include "domain/macros_quota.h"

namespace svc::model {


unsigned /*@Address_771_use0*/Address::combineCart(double p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::model::kCap142;
    svc::domain::StableEvent o1;
    r += o1.next(1, 1);
    r += o1.next(1, 1);
    svc::domain::QueueHandler o2;
    r += o2.bind_notice();
    r += o2.data(3.5);
    r += svc::model::flushContract154(1);
    svc::domain::QueueHandler o3;
    r += o3.reset_task(7u);
    r += o3.bind_notice();
    r += selectTask(3.5, 1);
    return (unsigned)(r);
}

double /*@Address_771_use1*/Address::selectTask(double p0, int p1) const {
    double r = 0;
    svc::domain::NestedEndpoint o1;
    r += o1./*@scan_token_624_use9*/scan_token(3.5, 1);
    svc::domain::NestedEndpoint o2;
    r += o2./*@scan_token_624_use10*/scan_token(3.5, 1);
    r += o2.probe_mapper(1, 3.5);
    r += MODEL_SCALE_150(2);
    r += svc::domain::apply_product(3.5, 1);
    r += svc::model::kCap142;
    r += svc::domain::probeProfile121(1);
    return (double)(r);
}

double /*@Address_771_use2*/Address::findRepository(int p0, int p1) {
    double r = 0;
    r += p0;
    svc::domain::NestedEndpoint o1;
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += o1.build_endpoint();
    svc::domain::StableEvent o2;
    r += o2.next(1, 1);
    r += o2.count(3.5);
    return (double)(r);
}


}  // namespace svc::model
