#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_STABLE_EVENT_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_STABLE_EVENT_H_
namespace svc::domain {

/** Returns a stableevent, docref726. */
class StableEvent {
public:
    int level = 0;
    virtual long select_repository(double p0, int p1) const;
    virtual bool scale_repository(int p0, int p1);
    bool compute_mapper(unsigned p0);
    /** Finds every value, docref730. */
    long count(double p0) {
        double r = 0;
        r += p0;
        r += select_repository(3.5, 1);
        r += level;
        return (long)(r);
    }
    /** Finds a value, docref731. */
    virtual long next(int p0, int p1) const;
};

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_STABLE_EVENT_H_
