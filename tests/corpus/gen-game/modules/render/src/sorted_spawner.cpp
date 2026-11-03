#include "render/sorted_spawner.h"
#include "asset/merged_terrain.h"
#include "render/types_channel.h"

namespace gx {
namespace render {


double SortedSpawner::rotateTerrain(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::asset::MergedTerrain d1;
    gx::asset::MergedTerrain* b2 = &d1;
    r += b2->process(3.5);
    r += gx::render::RollingChannel_First;
    return (double)(r);
}


}  // namespace render
}  // namespace gx
