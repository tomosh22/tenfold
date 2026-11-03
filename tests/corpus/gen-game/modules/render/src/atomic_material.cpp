#include "render/atomic_material.h"
#include "render/sorted_spawner.h"
#include "math/types_emitter.h"
#include "math/funcs_light.h"
#include "asset/types_arena.h"
#include "math/macros_skeleton.h"
#include "container/macros_script.h"
#include "math/chain14/c0.h"
#include "asset/cached_vector.h"
#include "asset/funcs_quat.h"
#include "math/types_prefab.h"
#include "asset/lazy_texture_sprite.h"

namespace gx {
namespace render {

static int fold(int p0) { return p0 + 9; }

long /*@AtomicMaterial_1094_use1*/AtomicMaterial::/*@apply_1096_def0*/apply() const {
    double r = 0;
    gx::render::SortedSpawner o1;
    r += o1.rotateTerrain(3.5, 1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::math::kBase28;
    r += gx::math::value(1, 1);
    gx::asset::/*@Size149_908_use2*/Size149 a2 = 1;
    r += a2;
    r += /*@MATH_JOINT_19_143_use7*/MATH_JOINT_19;
    r += this->total;
    return (long)(r);
}

unsigned /*@AtomicMaterial_1094_use2*/AtomicMaterial::probeQuat(int p0) const {
    double r = 0;
    r += /*@CONTAINER_SCENE_108_622_use7*/CONTAINER_SCENE_108;
    r += gx::math::chain14_entry(1);
    return (unsigned)(r);
}

bool /*@AtomicMaterial_1094_use3*/AtomicMaterial::saveMixer(long p0) {
    double r = 0;
    r += p0;
    r += gx::asset::CachedVector::shared_count;
    gx::render::SortedSpawner o1;
    r += o1.rotateTerrain(3.5, 1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += computeTerrain(3.5, 1);
    r += gx::asset::combineButton(2L);
    r += gx::math::kScale1;
    gx::asset::/*@LazyTextureSprite_1003_use3*/LazyTextureSprite<bool> t2;
    r += t2.get();
    t2.set(true);
    return (bool)(r);
}


}  // namespace render
}  // namespace gx
