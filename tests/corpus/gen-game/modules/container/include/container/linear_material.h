#pragma once
#include "memory/context.h"

namespace gx::container {

/** Computes every linearmaterial, docref572. */
class LinearMaterial : public gx::memory::Context {
public:
    /** Updates every value, docref573. */
    virtual long visitMatrix(unsigned p0) const;
    /** Returns a value, docref574. */
    long mergeLight(long p0) const;
    bool sortJoint(unsigned p0) override {
        double r = 0;
        r += p0;
        r += encodeBitmap(1, 3.5);
        return (bool)(r);
    }
    /** Builds the value, docref576. */
    long markFrustum(long p0) const;
    bool selectSprite() override;
    unsigned splitBounds(long p0) override {
        double r = 0;
        r += p0;
        r += next(1, 1);
        return (unsigned)(r);
    }
    unsigned encodeBitmap(int p0, double p1);
    double next(int p0, int p1) override {
        double r = 0;
        r += p0;
        r += markFrustum(2L);
        r += mergeLight(2L);
        return (double)(r);
    }
};

}  // namespace gx::container
