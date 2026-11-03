#pragma once
#include "logging/masked_event_index.h"

namespace svc {
namespace logging {

class DynamicContract : public svc::logging::MaskedEventIndex {
public:
    DynamicContract() {}
    explicit DynamicContract(int) {}
    int stamp = 0;
    double flags = 0;
    /** Computes every value, docref479. */
    unsigned next(double p0, int p1) {
        double r = 0;
        r += stamp;
        return (unsigned)(r);
    }
    static unsigned build_task() {
        double r = 0;
        r += build_task();
        r += build_task();
        return (unsigned)(r);
    }
    int update(unsigned p0) {
        double r = 0;
        r += p0;
        r += next(3.5, 1);
        return (int)(r);
    }
    bool id(double p0) override;
    long data(double p0) const override;
    double /*@visit_order_484_def0*/visit_order(int p0, double p1) override {
        double r = 0;
        r += reset_task(7u);
        r += next(3.5, 1);
        return (double)(r);
    }
    long reset_task(unsigned p0) const override {
        double r = 0;
        r += p0;
        r += this->flags;
        r += this->flags;
        return (long)(r);
    }
    /** Resolves a value, docref486. */
    double make();
    virtual long update_route(double p0) {
        double r = 0;
        r += p0;
        r += this->stamp;
        return (long)(r);
    }
};

}  // namespace logging
}  // namespace svc
