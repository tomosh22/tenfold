#include "math/primary_panel.h"
#include "math/trigger.h"
#include "math/cyclic_clip_prefab.h"
#include "math/radial_joint.h"
#include "math/types_font.h"
#include "math/types_prefab.h"
#include "math/funcs_light.h"
#include "math/chain14/c0.h"

namespace gx {
namespace math {


bool /*@PrimaryPanel_111_use3*/PrimaryPanel::filterScript(int p0, double p1) {
    double r = 0;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use31*/resolveBounds(7u);
    r += o1.verifyBody();
    r += o1.verifyBody();
    gx::math::/*@CyclicClipPrefab_33_use27*/CyclicClipPrefab o2;
    r += o2.count(3.5, 1);
    r += o2.combineSpawner(3.5, 1);
    gx::math::/*@CyclicClipPrefab_33_use28*/CyclicClipPrefab o3;
    r += o3.encodeVector(3.5);
    return (bool)(r);
}

bool /*@PrimaryPanel_111_use4*/PrimaryPanel::combineSpawner(double p0, int p1) {
    double r = 0;
    gx::math::/*@RadialJoint_42_use21*/RadialJoint o1;
    r += o1.scanFont(3.5, 1);
    r += o1.countSpawner(1);
    gx::math::MergedBatch k2 = gx::math::MergedBatch::Count;
    r += static_cast<int>(k2);
    gx::math::MergedBatch k3 = gx::math::MergedBatch::Count;
    r += static_cast<int>(k3);
    return (bool)(r);
}

long /*@PrimaryPanel_111_use5*/PrimaryPanel::visitAnimator(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += visitAnimator(1, 1);
    r += gx::math::kScale1;
    gx::math::/*@CyclicClipPrefab_33_use29*/CyclicClipPrefab d1;
    gx::math::/*@CyclicClipPrefab_33_use30*/CyclicClipPrefab& b2 = d1;
    r += b2.collectGlyph(3.5);
    return (long)(r);
}

int /*@PrimaryPanel_111_use6*/PrimaryPanel::splitPacket(int p0, double p1) const {
    double r = 0;
    r += p1;
    r += gx::math::next();
    r += static_cast<int>(gx::math::MergedBatch::Auto);
    r += items17.size();
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use32*/resolveBounds(7u);
    r += o1.verifyBody();
    r += gx::math::chain14_l0(1);
    return (int)(r);
}

long /*@PrimaryPanel_111_use7*/PrimaryPanel::fetchMixer() const {
    double r = 0;
    r += gx::math::next(1, 1);
    r += gx::math::Trigger::/*@shared_scale_11_use13*/shared_scale;
    r += gx::math::Trigger::/*@shared_scale_11_use14*/shared_scale;
    r += items17.size();
    return (long)(r);
}


}  // namespace math
}  // namespace gx
