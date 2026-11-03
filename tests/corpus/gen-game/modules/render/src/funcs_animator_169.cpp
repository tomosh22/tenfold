#include "render/funcs_animator_169.h"
#include "render/funcs_packet.h"
#include "render/types_channel.h"
#include "container/macros_script.h"
#include "render/options.h"
#include "render/dirty_terrain.h"
#include "asset/merged_light_panel.h"
#include "asset/context.h"
#include "math/radial_joint.h"
#include "render/atomic_material.h"
#include "render/funcs_batch.h"
#include "render/funcs_mesh.h"
#include "asset/funcs_frustum.h"
#include "asset/lazy_texture_sprite.h"

namespace gx {
namespace render {

double countArena(int p0, int p1) {
    double r = 0;
    r += gx::render::buildChannel(1);
    r += static_cast<int>(gx::render::PooledPrefab::Last);
    r += /*@CONTAINER_SCENE_108_622_use8*/CONTAINER_SCENE_108;
    gx::render::Options d1;
    gx::render::DirtyTerrain* b2 = &d1;
    r += b2->saveMixer(2L);
    return (double)(r);
}

long countArena(double p0) {
    double r = 0;
    gx::asset::/*@MergedLightPanel_1026_use6*/MergedLightPanel o1;
    r += /*@MergedLightPanel_1026_var0*/o1.value(1);
    r += gx::asset::Context::shared_scale;
    gx::math::/*@RadialJoint_42_use31*/RadialJoint o2;
    r += o2.width;
    r += o2.countSpawner(1);
    r += o2.collectGlyph(3.5);
    gx::render::/*@AtomicMaterial_1094_use5*/AtomicMaterial o3;
    r += /*@AtomicMaterial_1094_var0*/o3.computeTerrain(3.5, 1);
    r += o3.width;
    return (long)(r);
}

double splitCursor(int p0) {
    double r = 0;
    r += p0;
    r += gx::render::sweepFrame(2L);
    r += gx::render::scaleScript(2L);
    r += gx::render::set(1, 3.5);
    r += gx::render::reset();
    return (double)(r);
}

double top(int p0) {
    double r = 0;
    r += p0;
    r += gx::asset::name(3.5);
    r += gx::asset::collectBatch(2.5);
    return (double)(r);
}


}  // namespace render
}  // namespace gx
