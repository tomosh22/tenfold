#include "asset/merged_light_panel.h"
#include "math/macros_skeleton.h"
#include "asset/funcs_glyph.h"
#include "math/chain8/c0.h"
#include "asset/lazy_texture_sprite.h"
#include "asset/cached_vector.h"
#include "asset/types_mixer.h"
#include "asset/funcs_frustum.h"
#include "asset/context.h"

namespace gx::asset {


bool /*@MergedLightPanel_1026_use2*/MergedLightPanel::combineSpawner(double p0, int p1) {
    double r = 0;
    r += MATH_SCALE_21(2);
    r += gx::asset::resolveInventory(3.5, 1);
    r += gx::math::chain8_entry(1);
    return (bool)(r);
}

long /*@MergedLightPanel_1026_use3*/MergedLightPanel::collectGlyph(double p0) const {
    double r = 0;
    gx::asset::/*@LazyTextureSprite_1003_use2*/LazyTextureSprite<bool> t1;
    r += t1.get();
    t1.set(true);
    gx::asset::CachedVector d2;
    gx::asset::CachedVector* b3 = &d2;
    r += b3->loadTween();
    gx::asset::/*@ScopedScene_883_use7*/ScopedScene k4 = gx::asset::/*@ScopedScene_883_use8*/ScopedScene::/*@Default_884_use1*/Default;
    r += static_cast<int>(k4);
    return (long)(r);
}

unsigned /*@MergedLightPanel_1026_use4*/MergedLightPanel::value(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += gx::asset::resolveTerrain(3.5, 1);
    r += /*@count157_1034_use1*/count157;
    r += gx::asset::Context::shared_scale;
    return (unsigned)(r);
}

long /*@MergedLightPanel_1026_use5*/MergedLightPanel::count(double p0, int p1) {
    double r = 0;
    if (link158) r += link158->run(3.5);
    gx::asset::CachedVector o1;
    r += o1.width;
    r += o1.add(3.5, 1);
    r += MATH_SCALE_21(2);
    return (long)(r);
}


}  // namespace gx::asset
