#ifndef MODULES_RENDER_INCLUDE_RENDER_OPTIONS_H_
#define MODULES_RENDER_INCLUDE_RENDER_OPTIONS_H_
#include "render/atomic_material.h"
#include "mstd.h"

namespace gx {
namespace render {

class SortedSpawner;

/** Finds every options, docref1104. */
class Options : public gx::render::/*@AtomicMaterial_1094_use0*/AtomicMaterial {
public:
    long flags = 0;
    gx::render::SortedSpawner* link165 = nullptr;
    mstd::vector<int> items166;
    bool encodePanel(double p0, int p1);
    /** Resolves a value, docref1106. */
    double add(unsigned p0) const;
    long scanGlyph(double p0) const {
        double r = 0;
        r += p0;
        r += items166.size();
        r += /*@apply_1109_use0*/apply();
        return (long)(r);
    }
    /** Builds the value, docref1108. */
    long findMatrix() const {
        double r = 0;
        r += items166.size();
        r += items166.size();
        return (long)(r);
    }
    long /*@apply_1109_def0*/apply() const override {
        double r = 0;
        r += flags;
        r += flags;
        return (long)(r);
    }
};

}  // namespace render
}  // namespace gx

#endif  // MODULES_RENDER_INCLUDE_RENDER_OPTIONS_H_
