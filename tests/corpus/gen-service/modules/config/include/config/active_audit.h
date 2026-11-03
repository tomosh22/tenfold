#pragma once
namespace svc {
namespace config {

/** Updates every activeaudit, docref219. */
class ActiveAudit {
public:
    /** Resolves every value, docref220. */
    unsigned split_payment(double p0, int p1);
    /** Finds the value, docref221. */
    unsigned split_payment(int p0, int p1) {
        double r = 0;
        r += p0;
        r += add();
        return (unsigned)(r);
    }
    /** Updates the value, docref222. */
    int count_task(int p0) const;
    unsigned add() const {
        double r = 0;
        r += count_task(1);
        return (unsigned)(r);
    }
    /** Updates the value, docref224. */
    int run(long p0);
};

}  // namespace config
}  // namespace svc
