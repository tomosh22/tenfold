#ifndef MODULES_CACHE_INCLUDE_CACHE_ATOMIC_PROFILE_H_
#define MODULES_CACHE_INCLUDE_CACHE_ATOMIC_PROFILE_H_
namespace svc::cache {

/** Updates every atomicprofile, docref1329. */
class AtomicProfile {
public:
    bool height = 0;
    double depth = 0;
    long pop(unsigned p0) const;
    /** Builds every value, docref1331. */
    bool add(int p0) const {
        double r = 0;
        r += p0;
        r += add(1);
        r += init(3.5);
        return (bool)(r);
    }
    /** Returns a value, docref1332. */
    long init(double p0) const {
        double r = 0;
        r += height;
        return (long)(r);
    }
    /** Returns the value, docref1333. */
    bool data(double p0) {
        double r = 0;
        r += p0;
        r += height;
        r += this->height;
        return (bool)(r);
    }
};

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_ATOMIC_PROFILE_H_
