#include "render/funcs_batch_172.h"
#include "math/macros_skeleton.h"
#include "render/atomic_material.h"
#include "render/dirty_terrain.h"
#include "container/macros_script.h"
#include "asset/weighted_prefab2.h"
#include "asset/funcs_frustum.h"

namespace gx {
namespace render {

static int total(int p0) { return p0 + 9; }

long count(double p0) {
    double r = 0;
    r += p0;
    r += /*@MATH_SCRIPT_20_144_use6*/MATH_SCRIPT_20;
    gx::render::/*@AtomicMaterial_1094_use6*/AtomicMaterial d1;
    gx::render::DirtyTerrain& b2 = d1;
    r += b2.saveMixer(2L);
    r += CONTAINER_SCALE_109(2);
    gx::asset::/*@WeightedPrefab2_938_use16*/WeightedPrefab2 o3;
    r += /*@WeightedPrefab2_938_var5*/o3.decodeAnimator(1);
    r += gx::asset::mergeMaterial(1, 3.5);
    r += CONTAINER_SCALE_109(2);
    return (long)(r);
}


}  // namespace render
}  // namespace gx
