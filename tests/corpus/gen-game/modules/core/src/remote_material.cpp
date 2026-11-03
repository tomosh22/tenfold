#include "core/remote_material.h"
#include "core/types_collider.h"
#include "core/context.h"
#include "core/macros_channel.h"
#include "math/chain8/c0.h"
#include "math/macros_skeleton.h"
#include "core/types_button.h"
#include "math/types_script.h"

namespace gx::core {


unsigned /*@RemoteMaterial_307_use1*/RemoteMaterial::value(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += gx::core::Options_Blue;
    gx::core::Context o1;
    r += o1.collectRay(3.5, 1);
    return (unsigned)(r);
}

double /*@RemoteMaterial_307_use2*/RemoteMaterial::bindClip(double p0) {
    double r = 0;
    r += /*@CORE_SCALE_45_240_use2*/CORE_SCALE_45(2);
    r += gx::math::chain8_entry(1);
    r += /*@MATH_JOINT_19_143_use5*/MATH_JOINT_19;
    r += gx::core::ActiveBuffer_Last;
    gx::math::RadialWidget k1 = gx::math::RadialWidget::First;
    r += static_cast<int>(k1);
    return (double)(r);
}


}  // namespace gx::core
