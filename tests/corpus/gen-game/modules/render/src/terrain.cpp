#include "render/terrain.h"
#include "terrain_priv.h"
#include "render/sorted_spawner.h"
#include "render/types_channel.h"

namespace gx {
namespace render {

int /*@Terrain_1163_use2*/Terrain::shared_delta = 0;
static int mix(int p0) { return p0 + 4; }

long /*@Terrain_1163_use3*/Terrain::scanFrustum(long p0) const {
    double r = 0;
    r += p0;
    gx::render::SortedSpawner o1;
    r += o1.flush(3.5, 1);
    gx::render::/*@Id164_1078_use4*/Id164 a2 = 1;
    r += a2;
    return (long)(r);
}


}  // namespace render
}  // namespace gx
