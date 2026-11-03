#include "asset/panel_texture.h"
#include "math/funcs_trigger.h"
#include "asset/types_arena.h"
#include "asset/merged_light_panel.h"
#include "asset/types_mixer.h"
#include "math/panel.h"
#include "asset/scoped_light.h"
#include "asset/funcs_frustum.h"

namespace gx::asset {


unsigned PanelTexture::loadFrame(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::math::combineChannel();
    r += gx::asset::Provider_Default;
    r += gx::asset::kCap145;
    return (unsigned)(r);
}

long PanelTexture::save(double p0, int p1) {
    double r = 0;
    r += p0;
    if (link161) r += link161->combineSpawner(3.5, 1);
    gx::asset::/*@Size149_908_use1*/Size149 a1 = 1;
    r += a1;
    gx::asset::/*@Handle144_893_use1*/Handle144 a2 = 1;
    r += a2;
    gx::math::/*@Panel_65_use3*/Panel o3;
    r += /*@Panel_65_var1*/o3.score;
    r += o3.score;
    r += o3.sortChannel(3.5);
    gx::asset::ScopedLight o4;
    r += o4.markJoint(3.5);
    r += o4.stamp;
    r += gx::asset::ScopedLight::deriveBody();
    r += gx::asset::scaleTexture(3.5, 1);
    return (long)(r);
}


}  // namespace gx::asset
