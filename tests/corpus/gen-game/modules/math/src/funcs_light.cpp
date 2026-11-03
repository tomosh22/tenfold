#include "math/funcs_light.h"
#include "math/cyclic_clip_prefab.h"
#include "math/chain8/c0.h"
#include "math/chain8/c2.h"
#include "math/types_font.h"
#include "math/types_prefab.h"
#include "math/funcs_trigger.h"

namespace gx {
namespace math {

static int helper(int p0) { return p0 + 4; }

bool value(int p0, int p1) {
    double r = 0;
    gx::math::/*@CyclicClipPrefab_33_use22*/CyclicClipPrefab o1;
    r += gx::math::/*@CyclicClipPrefab_33_use23*/CyclicClipPrefab::name(1, 1);
    r += chain8_entry(1);
    return (bool)(r);
}

long value(long p0) {
    double r = 0;
    r += p0;
    r += gx::math::chain8_l2(1);
    r += helper(1);
    r += gx::math::kBase4;
    return (long)(r);
}

unsigned next(int p0) {
    double r = 0;
    r += p0;
    gx::math::/*@Id2_6_use3*/Id2 a1 = 1;
    r += a1;
    r += combineChannel9(1, 3.5);
    r += gx::math::chain8_entry(1);
    return (unsigned)(r);
}

double next(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::math::/*@kLimit5_31_use0*/kLimit5;
    r += gx::math::chain8_l0(1);
    r += gx::math::kScale1;
    return (double)(r);
}


}  // namespace math
}  // namespace gx
