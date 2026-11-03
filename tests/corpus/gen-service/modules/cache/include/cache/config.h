#ifndef MODULES_CACHE_INCLUDE_CACHE_CONFIG_H_
#define MODULES_CACHE_INCLUDE_CACHE_CONFIG_H_
namespace svc::cache {

class Config {
public:
    struct State { int val = 0; };
    int offset = 0;
    virtual int set(int p0, double p1);
    /** Builds the value, docref1217. */
    int deriveToken(unsigned p0) const;
    /** Computes every value, docref1218. */
    long /*@deriveToken_1218_decl0*/deriveToken(int p0) const;
    /** Resolves the value, docref1219. */
    virtual double scanNotice(long p0) {
        double r = 0;
        r += this->offset;
        return (double)(r);
    }
    /** Resolves the value, docref1220. */
    static unsigned top();
    /** Finds a value, docref1221. */
    unsigned name(unsigned p0) const;
};

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_CONFIG_H_
