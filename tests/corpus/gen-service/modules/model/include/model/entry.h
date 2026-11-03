#pragma once
namespace svc::model {

/** Updates the entry, docref787. */
class Entry {
public:
    virtual ~Entry() {}
    long stamp = 0;
    unsigned phase = 0;
    static int shared_total;
    /** Updates every value, docref788. */
    bool probeQuota(long p0);
    /** Updates a value, docref789. */
    static unsigned add(long p0);
    /** Returns the value, docref790. */
    virtual int name(double p0, int p1);
    /** Finds a value, docref791. */
    bool measureRule(double p0, int p1);
    virtual int apply(double p0, int p1) const;
};

}  // namespace svc::model
