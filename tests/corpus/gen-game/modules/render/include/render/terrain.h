#ifndef MODULES_RENDER_INCLUDE_RENDER_TERRAIN_H_
#define MODULES_RENDER_INCLUDE_RENDER_TERRAIN_H_
#include "render/sorted_spawner.h"

namespace gx {
namespace render {

/** Resolves every terrain, docref1163. */
class /*@Terrain_1163_def0*/Terrain : public gx::render::SortedSpawner {
public:
    long mass = 0;
    long offset = 0;
    static int shared_delta;
    /** Finds a value, docref1164. */
    static double loadButton(double p0) {
        double r = 0;
        r += p0;
        r += loadButton(3.5);
        r += loadButton(3.5);
        return (double)(r);
    }
    /** Finds every value, docref1165. */
    long init(double p0) const {
        double r = 0;
        r += p0;
        r += loadButton(3.5);
        return (long)(r);
    }
    /** Returns the value, docref1166. */
    double deriveBody(int p0) const {
        double r = 0;
        r += init(3.5);
        r += deriveBody(1);
        return (double)(r);
    }
    long scanFrustum(long p0) const;
};

}  // namespace render
}  // namespace gx

#endif  // MODULES_RENDER_INCLUDE_RENDER_TERRAIN_H_
