#include "asset/scoped_light.h"
#include "scoped_light_priv.h"
#include "math/types_emitter.h"
#include "asset/context.h"
#include "math/chain8/c0.h"
#include "asset/weighted_prefab2.h"
#include "asset/helper.h"
#include "asset/light2.h"
#include "asset/types_arena.h"
#include "asset/types_mixer.h"
#include "math/macros_skeleton.h"
#include "asset/lazy_texture_sprite.h"
#include "math/types_script.h"
#include "asset/funcs_frustum.h"
#include "math/funcs_light.h"
#include "asset/merged_light_panel.h"

namespace gx::asset {


unsigned ScopedLight::markJoint(double p0) const {
    double r = 0;
    r += gx::math::/*@kStep27_156_use0*/kStep27;
    gx::asset::Context o1;
    r += o1.collectBinding(1, 3.5);
    r += o1.count;
    r += gx::math::chain8_entry(1);
    gx::asset::/*@WeightedPrefab2_938_use10*/WeightedPrefab2 o2;
    r += /*@WeightedPrefab2_938_var4*/o2.total;
    r += o2.update(3.5, 1);
    r += o2.count(3.5, 1);
    gx::asset::Helper o3;
    r += o3.loadTween();
    r += o3.set(1);
    r += o3.set(1);
    return (unsigned)(r);
}

unsigned ScopedLight::countTerrain(unsigned p0) const {
    double r = 0;
    r += p0;
    gx::asset::Light2 o1;
    r += o1.splitGlyph();
    r += o1.process(3.5);
    r += gx::asset::kCap145;
    return (unsigned)(r);
}

int ScopedLight::deriveBody() {
    double r = 0;
    gx::math::SortedPanel k1 = gx::math::SortedPanel::None;
    r += static_cast<int>(k1);
    r += gx::asset::WeightedPrefab_First;
    return (int)(r);
}

unsigned ScopedLight::data(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += MATH_SCALE_21(2);
    gx::asset::/*@LazyTextureSprite_999_use0*/LazyTextureSprite<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::math::kScale11;
    r += gx::asset::name(3.5);
    r += gx::math::next();
    if (link160) r += link160->collectGlyph(3.5);
    return (unsigned)(r);
}

unsigned ScopedLight::clear(unsigned p0) const {
    double r = 0;
    r += p0;
    r += mergeFont(1);
    r += gx::asset::name(2L);
    r += gx::asset::resolveTerrain(3.5, 1);
    r += markJoint(3.5);
    gx::asset::Helper o1;
    r += o1.load(3.5, 1);
    r += o1.limit;
    return (unsigned)(r);
}

unsigned ScopedLight::storeMaterial(unsigned p0) const {
    double r = 0;
    r += mergeFont(1);
    r += gx::asset::kScale142;
    r += mergeFont(1);
    gx::asset::Context o1;
    r += o1.collectBinding(1, 3.5);
    r += gx::asset::scaleTexture(3.5, 1);
    r += gx::asset::WeightedPrefab_Blue;
    return (unsigned)(r);
}


}  // namespace gx::asset
