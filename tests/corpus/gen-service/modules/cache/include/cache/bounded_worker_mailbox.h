#pragma once
namespace svc::cache {

class Invoice;

/** Updates the boundedworkermailbox, docref1282. */
class BoundedWorkerMailbox {
public:
    bool rank = 0;
    bool phase = 0;
    svc::cache::Invoice* link267 = nullptr;
    /** Updates a value, docref1283. */
    bool verifyProfile(long p0) const;
    /** Returns the value, docref1284. */
    bool resetQuota(double p0);
    int shiftTariff(int p0, double p1) {
        double r = 0;
        r += p1;
        r += resetQuota(3.5);
        r += phase;
        return (int)(r);
    }
};

}  // namespace svc::cache
