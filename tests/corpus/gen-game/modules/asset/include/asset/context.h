#ifndef MODULES_ASSET_INCLUDE_ASSET_CONTEXT_H_
#define MODULES_ASSET_INCLUDE_ASSET_CONTEXT_H_
namespace gx::asset {

class Context {
public:
    double mass = 0;
    int depth = 0;
    unsigned count = 0;
    static int shared_scale;
    long collectBinding(int p0, double p1) const {
        double r = 0;
        r += p1;
        r += this->count;
        return (long)(r);
    }
    /** Finds every value, docref979. */
    unsigned collectBinding(int p0) const;
    /** Finds the value, docref980. */
    unsigned collectBinding(int p0, int p1) const;
    /** Builds every value, docref981. */
    unsigned run(double p0) const;
    double apply(double p0) const;
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_CONTEXT_H_
