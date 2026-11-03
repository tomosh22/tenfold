#include "asset/light2.h"
#include "asset/funcs_quat.h"
#include "asset/helper.h"
#include "math/chain8/c0.h"
#include "asset/types_mixer.h"
#include "math/chain8/c2.h"
#include "asset/cached_vector.h"
#include "asset/types_arena.h"
#include "asset/funcs_frustum.h"
#include "math/macros_skeleton.h"

namespace gx::asset {

static int sum(int p0) { return p0 + 7; }

double Light2::verifyTween(int p0, int p1) {
    double r = 0;
    r += p0;
    r += gx::asset::find(3.5, 1);
    gx::asset::Helper o1;
    r += o1.limit;
    r += gx::math::chain8_entry(1);
    r += gx::asset::kScale143;
    r += process(3.5);
    return (double)(r);
}

double Light2::applyQuest(int p0) {
    double r = 0;
    r += gx::math::chain8_l2(1);
    r += weight;
    return (double)(r);
}

int Light2::process(double p0) const {
    double r = 0;
    r += p0;
    gx::asset::CachedVector o1;
    r += o1.loadTween();
    r += o1.resetInventory(1, 3.5);
    r += sum(1);
    r += sum(1);
    r += gx::asset::kCap145;
    return (int)(r);
}

int Light2::splitGlyph() {
    double r = 0;
    gx::asset::CachedVector d1;
    gx::asset::CachedVector* b2 = &d1;
    r += b2->visitWidget();
    gx::asset::Helper o3;
    r += o3.visitWidget();
    r += o3.verifyButton(1, 3.5);
    r += o3.verifyButton(1, 3.5);
    r += gx::asset::push(1, 3.5);
    r += static_cast<int>(gx::asset::/*@QueuedMixer_899_use0*/QueuedMixer::Auto);
    return (int)(r);
}

long Light2::selectWidget(int p0) {
    double r = 0;
    r += gx::asset::encodeBinding(1, 1);
    r += gx::asset::filterGlyph(2L);
    r += gx::asset::Provider_Last;
    r += /*@MATH_SCRIPT_20_144_use3*/MATH_SCRIPT_20;
    r += sum(1);
    r += gx::asset::mergeMaterial(1, 1);
    return (long)(r);
}


}  // namespace gx::asset
