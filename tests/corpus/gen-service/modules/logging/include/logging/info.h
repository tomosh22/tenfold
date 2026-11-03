#ifndef MODULES_LOGGING_INCLUDE_LOGGING_INFO_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_INFO_H_
#include "mstd.h"

namespace svc {
namespace logging {

class /*@Registry_413_fwd0*/Registry;

class Info {
public:
    bool offset = 0;
    double /*@score_446_def0*/score = 0;
    double phase = 0;
    svc::logging::/*@Registry_413_use7*/Registry* link80 = nullptr;
    mstd::vector<int> items81;
    /** Computes every value, docref440. */
    int set(int p0, double p1);
    /** Builds every value, docref441. */
    bool top(unsigned p0) {
        double r = 0;
        r += p0;
        r += set(1, 3.5);
        return (bool)(r);
    }
    /** Computes the value, docref442. */
    double combine_queue(int p0);
    /** Returns a value, docref443. */
    unsigned update_audit(unsigned p0);
    unsigned verify_shipment(int p0, int p1) const;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_INFO_H_
