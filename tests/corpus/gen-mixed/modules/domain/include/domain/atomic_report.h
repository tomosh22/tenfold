#pragma once
namespace mx::domain {

/** Finds every atomicreport, docref710. */
class AtomicReport {
public:
    /** Computes the value, docref711. */
    long loadToken(int p0);
    double measureSchedule(long p0) {
        double r = 0;
        r += measureSchedule(2L);
        r += loadToken(1);
        return (double)(r);
    }
};

}  // namespace mx::domain
