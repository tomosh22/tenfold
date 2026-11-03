#include "render/funcs_widget.h"
#include "container/shader.h"
#include "render/terrain.h"
#include "asset/merged_terrain.h"
#include "render/sorted_spawner.h"
#include "render/funcs_mesh.h"
#include "render/types_channel.h"

namespace gx {
namespace render {

long loadJoint(int p0, int p1) {
    double r = 0;
    r += p1;
    r += gx::container::findScript<int>(2);
    gx::render::/*@Terrain_1163_use1*/Terrain::shared_delta++;
    return (long)(r);
}

unsigned combineTween(int p0, double p1) {
    double r = 0;
    gx::asset::MergedTerrain o1;
    r += o1.score;
    r += o1.weight;
    r += o1.process(3.5);
    gx::render::SortedSpawner o2;
    r += o2.rotateTerrain(3.5, 1);
    r += o2.flush(3.5, 1);
    r += visitJoint(1, 3.5);
    gx::render::/*@Id164_1078_use3*/Id164 a3 = 1;
    r += a3;
    return (unsigned)(r);
}


}  // namespace render
}  // namespace gx
