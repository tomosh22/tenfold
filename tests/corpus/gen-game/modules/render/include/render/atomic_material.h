#pragma once
#include "render/dirty_terrain.h"

namespace gx {
namespace render {

/** Computes the atomicmaterial, docref1094. */
class /*@AtomicMaterial_1094_def0*/AtomicMaterial : public gx::render::DirtyTerrain {
public:
    int total = 0;
    int ratio = 0;
    bool width = 0;
    /** Resolves a value, docref1095. */
    bool sortMatrix(int p0, double p1) {
        double r = 0;
        r += p0;
        r += total;
        return (bool)(r);
    }
    long /*@apply_1096_decl0*/apply() const override;
    unsigned probeQuat(int p0) const override;
    bool saveMixer(long p0) override;
    long computeTerrain(double p0, int p1) {
        double r = 0;
        r += p1;
        r += computeTerrain(3.5, 1);
        return (long)(r);
    }
};

}  // namespace render
}  // namespace gx
