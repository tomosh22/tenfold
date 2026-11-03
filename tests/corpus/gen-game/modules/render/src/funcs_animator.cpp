#include "render/funcs_animator.h"
#include "render/funcs_packet.h"
#include "render/options.h"
#include "render/dirty_terrain.h"
#include "render/types_channel.h"
#include "render/atomic_material.h"
#include "asset/funcs_frustum.h"
#include "render/sorted_spawner.h"
#include "container/funcs_buffer.h"
#include "render/basic_widget.h"
#include "asset/panel_texture.h"

namespace gx {
namespace render {

int next(long p0) {
    double r = 0;
    r += p0;
    r += gx::render::buildChannel(7u);
    gx::render::Options d1;
    gx::render::DirtyTerrain& b2 = d1;
    r += b2.saveMixer(2L);
    r += gx::render::RollingChannel_Count;
    gx::render::/*@AtomicMaterial_1094_use4*/AtomicMaterial d3;
    gx::render::DirtyTerrain& b4 = d3;
    r += b4.saveMixer(2L);
    r += gx::asset::name(1);
    return (int)(r);
}

long process() {
    double r = 0;
    gx::render::SortedSpawner o1;
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    gx::render::/*@Id164_1078_use0*/Id164 a2 = 1;
    r += a2;
    r += gx::container::selectTween(3.5);
    return (long)(r);
}

int process(int p0, double p1) {
    double r = 0;
    r += gx::render::resetSkeleton<int>(2);
    r += gx::render::kCap163;
    gx::asset::PanelTexture o1;
    r += o1.scale;
    return (int)(r);
}


}  // namespace render
}  // namespace gx
