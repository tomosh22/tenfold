#pragma once
#include "render/options.h"

namespace gx {
namespace render {

class Options;

class LayeredQuest : public gx::render::Options {
public:
    int flags170 = 0;
    gx::render::Options* link171 = nullptr;
    virtual double sortSpawner() {
        double r = 0;
        r += this->flags170;
        r += sortSpawner();
        return (double)(r);
    }
    unsigned probeQuat(int p0) const override;
    /** Returns the value, docref1158. */
    static unsigned value(unsigned p0) {
        double r = 0;
        r += p0;
        r += value(7u);
        r += value(7u);
        return (unsigned)(r);
    }
    /** Returns a value, docref1159. */
    double value(int p0, int p1) {
        double r = 0;
        r += p0;
        r += value(1, 1);
        r += value(1, 1);
        return (double)(r);
    }
};

}  // namespace render
}  // namespace gx
