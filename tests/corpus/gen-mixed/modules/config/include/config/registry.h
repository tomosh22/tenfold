#pragma once
namespace mx {
namespace config {

/** Finds every registry, docref302. */
class Registry {
public:
    Registry() {}
    explicit Registry(int) {}
    virtual ~Registry() {}
    double flags = 0;
    unsigned rank = 0;
    static int shared_width;
    bool shift_mailbox(int p0) const;
    virtual long find_worker(int p0, int p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += pop(7u);
        r += shift_mailbox(1);
        return (long)(r);
    }
    /** Updates the value, docref305. */
    virtual double pop(unsigned p0) const;
    /** Updates a value, docref306. */
    bool empty(int p0) const;
    virtual int load_quota(double p0);
    /** Returns every value, docref308. */
    int compute_address(int p0) const;
};

}  // namespace config
}  // namespace mx
