#include "render/funcs_mesh.h"
#include "render/basic_widget.h"
#include "render/atomic_material.h"
#include "render/dirty_terrain.h"
#include "math/types_emitter.h"
#include "asset/cached_vector.h"
#include "asset/types_arena.h"
#include "container/funcs_terrain.h"
#include "container/tiled_light_frame.h"
#include "render/funcs_packet.h"
#include "render/sorted_spawner.h"
#include "render/options.h"
#include "asset/funcs_frustum.h"
#include "math/matrix.h"
#include "render/types_channel.h"

namespace gx {
namespace render {

bool buildMaterial(double p0) {
    double r = 0;
    r += p0;
    r += gx::render::resetSkeleton<int>(2);
    gx::render::/*@AtomicMaterial_1094_use7*/AtomicMaterial d1;
    gx::render::DirtyTerrain* b2 = &d1;
    r += b2->probeQuat(1);
    gx::render::/*@AtomicMaterial_1094_use8*/AtomicMaterial d3;
    gx::render::DirtyTerrain* b4 = &d3;
    r += b4->/*@apply_1081_use0*/apply();
    r += gx::math::kScale26;
    gx::asset::CachedVector::shared_count++;
    return (bool)(r);
}

bool visitJoint(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::render::/*@AtomicMaterial_1094_use9*/AtomicMaterial o1;
    r += /*@AtomicMaterial_1094_var1*/o1.ratio;
    r += o1.total;
    gx::render::/*@AtomicMaterial_1094_use10*/AtomicMaterial o2;
    r += /*@AtomicMaterial_1094_var2*/o2.ratio;
    r += o2.probeQuat(1);
    r += gx::asset::kScale146;
    r += gx::container::selectWidget();
    return (bool)(r);
}

long visitJoint(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::container::TiledLightFrame::/*@shared_scale_520_use8*/shared_scale;
    r += gx::render::buildChannel(1);
    gx::render::SortedSpawner o1;
    r += o1.flush(3.5, 1);
    r += o1.flush(3.5, 1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    gx::render::Options d2;
    gx::render::/*@AtomicMaterial_1094_use11*/AtomicMaterial& b3 = d2;
    r += b3.probeQuat(1);
    return (long)(r);
}

double bindQuest(int p0, int p1) {
    double r = 0;
    r += p1;
    r += gx::asset::name(2L);
    gx::render::Options d1;
    gx::render::Options* b2 = &d1;
    r += b2->probeQuat(1);
    r += gx::render::buildChannel(1);
    return (double)(r);
}

bool bindQuest(double p0, int p1) {
    double r = 0;
    r += gx::math::Matrix::shared_speed;
    gx::math::Matrix::shared_speed++;
    r += gx::render::resetCollider(1, 3.5);
    gx::render::/*@Id164_1078_use1*/Id164 a1 = 1;
    r += a1;
    gx::render::BasicWidget<int> t2;
    r += t2.get();
    t2.set(3);
    return (bool)(r);
}


}  // namespace render
}  // namespace gx
