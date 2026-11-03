#include "render/options.h"
#include "render/funcs_packet.h"
#include "math/funcs_light.h"
#include "render/sorted_spawner.h"

namespace gx {
namespace render {

static int clamp(int p0) { return p0 + 2; }

bool Options::encodePanel(double p0, int p1) {
    double r = 0;
    r += gx::render::buildChannel(1);
    r += gx::math::value(1, 1);
    return (bool)(r);
}

double Options::add(unsigned p0) const {
    double r = 0;
    r += p0;
    gx::render::SortedSpawner o1;
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += o1.rotateTerrain(3.5, 1);
    r += gx::render::buildChannel(1);
    r += clamp(1);
    return (double)(r);
}


}  // namespace render
}  // namespace gx
