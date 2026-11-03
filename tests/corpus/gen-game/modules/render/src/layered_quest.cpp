#include "render/layered_quest.h"
#include "render/basic_widget.h"
#include "asset/helper.h"
#include "asset/types_arena.h"
#include "math/chain8/c0.h"

namespace gx {
namespace render {

static int step(int p0) { return p0 + 4; }

unsigned LayeredQuest::probeQuat(int p0) const {
    double r = 0;
    r += gx::render::scaleCurve<int>(2);
    gx::asset::Helper o1;
    r += o1.saveFrame(1);
    r += gx::asset::kCap145;
    r += gx::math::chain8_entry(1);
    return (unsigned)(r);
}


}  // namespace render
}  // namespace gx
