#ifndef MODULES_CORE_INCLUDE_CORE_CONTEXT_H_
#define MODULES_CORE_INCLUDE_CORE_CONTEXT_H_
namespace gx::core {

/** Returns a context, docref234. */
class Context {
public:
    unsigned probeArena(double p0, int p1) {
        double r = 0;
        r += collectRay(3.5, 1);
        r += probeArena(3.5, 1);
        return (unsigned)(r);
    }
    virtual bool collectRay(double p0, int p1) const;
    int count(int p0, double p1) const {
        double r = 0;
        r += p1;
        r += count(1, 3.5);
        r += collectRay(3.5, 1);
        return (int)(r);
    }
};

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_CONTEXT_H_
