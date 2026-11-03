#include "render/funcs_packet.h"
#include "render/types_channel.h"
#include "container/funcs_terrain.h"
#include "container/macros_script.h"
#include "asset/lazy_texture_sprite.h"
#include "asset/context.h"
#include "math/funcs_trigger.h"
#include "container/linear_material.h"

namespace gx {
namespace render {

unsigned resetCollider(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::render::RollingChannel_First;
    r += gx::render::kCap162;
    r += gx::container::markCamera(1, 3.5);
    gx::render::/*@Id164_1078_use2*/Id164 a1 = 1;
    r += a1;
    return (unsigned)(r);
}

long decodeTrigger(unsigned p0) {
    double r = 0;
    r += CONTAINER_SCALE_109(2);
    r += gx::asset::collectBatch<int>(2);
    gx::asset::Context o1;
    r += o1.mass;
    r += o1.collectBinding(1, 1);
    gx::asset::/*@LazyTextureSprite_999_use1*/LazyTextureSprite<int> t2;
    r += t2.get();
    t2.set(3);
    return (long)(r);
}

int set(int p0, double p1) {
    double r = 0;
    r += p0;
    r += gx::math::combineChannel9(1);
    gx::container::LinearMaterial o1;
    r += o1.visitMatrix(7u);
    r += gx::container::/*@selectButton_542_use2*/selectButton(1, 3.5);
    return (int)(r);
}


}  // namespace render
}  // namespace gx
