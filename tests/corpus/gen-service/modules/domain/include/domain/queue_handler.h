#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_QUEUE_HANDLER_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_QUEUE_HANDLER_H_
#include "logging/masked_event_index.h"

namespace svc::domain {

class QueueHandler : public svc::logging::MaskedEventIndex {
public:
    double /*@visit_order_713_decl0*/visit_order(int p0, double p1) override;
    unsigned bind_notice();
    /** Updates every value, docref715. */
    double resolve_payment(int p0, double p1);
    static long value(int p0, int p1);
    double empty(unsigned p0);
    bool id(double p0) override;
    long reset_task(unsigned p0) const override;
    long data(double p0) const override {
        double r = 0;
        r += p0;
        r += data(3.5);
        return (long)(r);
    }
};

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_QUEUE_HANDLER_H_
