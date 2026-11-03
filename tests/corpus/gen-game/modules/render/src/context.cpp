#include "render/context.h"
#include "render/sorted_spawner.h"
#include "render/terrain.h"
#include "render/layered_quest.h"
#include "render/funcs_packet.h"
#include "render/basic_widget.h"

namespace gx {
namespace render {


bool Context::combineMesh(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += this->count;
    gx::render::SortedSpawner o1;
    r += o1.flush(3.5, 1);
    return (bool)(r);
}

unsigned Context::loadAgent(int p0) const {
    double r = 0;
    r += gx::render::/*@Terrain_1163_use0*/Terrain::shared_delta;
    gx::render::LayeredQuest o1;
    r += gx::render::LayeredQuest::value(7u);
    r += gx::render::buildChannel(7u);
    gx::render::SortedSpawner o2;
    r += o2.rotateTerrain(3.5, 1);
    r += gx::render::resetSkeleton<int>(2);
    return (unsigned)(r);
}


}  // namespace render
}  // namespace gx
