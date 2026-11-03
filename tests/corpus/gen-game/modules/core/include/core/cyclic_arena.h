#ifndef MODULES_CORE_INCLUDE_CORE_CYCLIC_ARENA_H_
#define MODULES_CORE_INCLUDE_CORE_CYCLIC_ARENA_H_
namespace gx::core {

/** Builds a cyclicarena, docref254. */
class CyclicArena {
public:
    /** Returns every value, docref255. */
    static unsigned process();
    virtual bool get(unsigned p0) const {
        double r = 0;
        r += resolveShader();
        r += resolveShader();
        return (bool)(r);
    }
    virtual unsigned saveButton(int p0, double p1);
    virtual double resolveShader() const;
    /** Updates every value, docref259. */
    int mergeArena(int p0, double p1);
};

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_CYCLIC_ARENA_H_
