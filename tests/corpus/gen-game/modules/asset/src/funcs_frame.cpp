#include "asset/funcs_frame.h"
#include "math/types_emitter.h"
#include "asset/funcs_frustum.h"
#include "asset/lazy_texture_sprite.h"
#include "asset/merged_terrain.h"
#include "asset/types_arena.h"
#include "math/bounded_pool_spawner.h"
#include "asset/cached_vector.h"

namespace gx::asset {

unsigned loadTexture(unsigned p0) {
    double r = 0;
    r += gx::math::kBase28;
    r += gx::asset::name(2L);
    gx::asset::/*@LazyTextureSprite_1003_use1*/LazyTextureSprite<bool> t1;
    r += t1.get();
    t1.set(true);
    r += mergeMaterial(1, 3.5);
    return (unsigned)(r);
}

unsigned loadTexture(int p0, int p1) {
    double r = 0;
    r += p0;
    gx::asset::MergedTerrain o1;
    r += o1.weight;
    r += gx::asset::kCap145;
    return (unsigned)(r);
}

long clear(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::math::/*@BoundedPoolSpawner_98_use2*/BoundedPoolSpawner o1;
    r += /*@BoundedPoolSpawner_98_var2*/o1.shiftFont(7u);
    r += o1./*@markTerrain_107_use1*/markTerrain();
    r += o1./*@markTerrain_107_use2*/markTerrain();
    gx::asset::CachedVector d2;
    gx::asset::CachedVector& b3 = d2;
    r += b3.loadTween();
    return (long)(r);
}


}  // namespace gx::asset
