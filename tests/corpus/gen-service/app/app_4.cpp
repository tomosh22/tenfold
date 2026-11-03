#include "app.h"
#include "config/config.h"
#include "cache/cache.h"
#include "cache/config.h"
#include "repo/frozen_repository_customer.h"
#include "cache/atomic_profile.h"
#include "cache/types_cart.h"
#include "storage/funcs_endpoint.h"
#include "repo/funcs_queue.h"
#include "storage/macros_schedule.h"
#include "domain/queue_handler.h"
#include "config/sampled_endpoint_gateway.h"
#include "storage/macros_tenant.h"
#include "storage/job2.h"
#include "domain/config.h"
#include "repo/macros_task.h"
#include "logging/funcs_mapper.h"
#include "repo/types_address.h"
#include "storage/node.h"
#include "util/buffer.h"
#include "logging/funcs_policy.h"
#include "config/types_session.h"
#include "config/macros_route.h"
#include "repo/types_product.h"
#include "domain/funcs_quota.h"

double appRun282() {
    double r = 0;
    svc::cache::Config o1;
    r += o1.name(7u);
    svc::repo::FrozenRepositoryCustomer d2;
    svc::repo::FrozenRepositoryCustomer& b3 = d2;
    r += b3.sweep_quota(2L);
    svc::cache::AtomicProfile o4;
    r += o4.height;
    r += o4.add(1);
    svc::cache::Schedule k5 = svc::cache::Schedule::Blue;
    r += static_cast<int>(k5);
    r += svc::storage::resetMapper(1, 3.5);
    r += svc::repo::saveMailbox();
    r += svc::storage::/*@markCatalog235_1148_use0*/markCatalog235(1);
    svc::domain::QueueHandler o6;
    r += o6.empty(7u);
    r += o6.reset_task(7u);
    r += o6.id(3.5);
    svc::config::/*@SampledEndpointGateway_333_use1*/SampledEndpointGateway d7;
    svc::config::/*@SampledEndpointGateway_333_use2*/SampledEndpointGateway* b8 = &d7;
    r += b8->combineCustomer(1, 1);
    r += STORAGE_CUSTOMER_221;
    svc::storage::Job2 o9;
    r += o9.flushSession(1, 3.5);
    return r;
}

double /*@appRun283_1360_def0*/appRun283() {
    double r = 0;
    svc::domain::/*@Config_543_use1*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += REPO_SCALE_199(2);
    r += svc::logging::update(2L);
    svc::repo::Real189 a2 = 1;
    r += a2;
    svc::domain::/*@Config_543_use2*/Config<int> t3;
    r += t3.get();
    t3.set(3);
    svc::storage::/*@Node_1077_use2*/Node d4;
    svc::util::Buffer* b5 = &d4;
    r += b5->combineShipment(3.5);
    r += svc::logging::update_policy(3.5, 1);
    svc::config::Size61 a6 = 1;
    r += a6;
    r += svc::config::shiftEndpoint58(1);
    r += static_cast<int>(svc::repo::/*@SharedRoute_887_use0*/SharedRoute::Auto);
    r += svc::domain::count_quota(1);
    return r;
}

