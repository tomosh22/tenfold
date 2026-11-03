#pragma once
namespace gx::io {

/** Resolves the provider, docref856. */
class Provider {
public:
    /** Computes every value, docref857. */
    unsigned selectTween(double p0, int p1) const;
    bool verifyMatrix(unsigned p0);
    /** Builds every value, docref859. */
    static bool flush(double p0) {
        double r = 0;
        r += p0;
        r += flush(3.5);
        return (bool)(r);
    }
};

}  // namespace gx::io
