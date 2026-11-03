#pragma once
#include "core/context.h"
#include "mstd.h"

namespace gx::memory {

class /*@Buffer_386_fwd0*/Buffer;

class /*@RollingRayGlyph_392_def0*/RollingRayGlyph : public gx::core::Context {
public:
    gx::memory::/*@Buffer_386_use11*/Buffer* link75 = nullptr;
    mstd::vector<int> items76;
    static bool rotateAnimator(long p0);
    /** Returns a value, docref394. */
    double fetchScript(int p0, int p1) const;
    unsigned rotateAnimator(int p0, double p1);
    virtual double markTrigger() const;
};

}  // namespace gx::memory
