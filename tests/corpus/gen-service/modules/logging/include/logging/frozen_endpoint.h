#ifndef MODULES_LOGGING_INCLUDE_LOGGING_FROZEN_ENDPOINT_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_FROZEN_ENDPOINT_H_
#include "util/context.h"

namespace svc {
namespace logging {

/** Returns every frozenendpoint, docref509. */
class FrozenEndpoint : public svc::util::/*@Context_184_use2*/Context {
public:
    double push(int p0, int p1);
    /** Returns every value, docref511. */
    long reset(int p0, double p1) const;
    /** Returns the value, docref512. */
    double apply(int p0) const;
    virtual double sweep_quota(long p0) const;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_FROZEN_ENDPOINT_H_
