#include "shader/funcs_bounds.h"
#include "io/dynamic_channel.h"
#include "io/funcs_light.h"
#include "io/packet.h"
#include "shader/types_trigger.h"
#include "io/types_vector.h"
#include "io/funcs_camera.h"
#include "io/bounds2.h"
#include "io/funcs_terrain.h"
#include "io/rigid_quest.h"
#include "io/table141.h"
#include "io/panel2.h"

namespace gx {
namespace shader {

static int /*@clamp_1199_def0*/clamp(int p0) { return p0 + 9; }

unsigned size(int p0, int p1) {
    double r = 0;
    r += p0;
    gx::io::DynamicChannel o1;
    r += o1.bindFrame(1, 1);
    r += o1.updateChannel(1, 3.5);
    r += gx::io::add();
    gx::io::Packet o2;
    r += o2./*@encodeMesh_805_use1*/encodeMesh(7u);
    return (unsigned)(r);
}

double rotateArena(int p0, double p1) {
    double r = 0;
    r += p0;
    gx::shader::Size175 a1 = 1;
    r += a1;
    r += gx::shader::kCap174;
    return (double)(r);
}

long applyPanel(unsigned p0) {
    double r = 0;
    r += p0;
    r += gx::io::/*@kStep116_662_use1*/kStep116;
    r += gx::io::pop(1, 1);
    r += gx::io::add(2L);
    return (long)(r);
}

bool collectEmitter(int p0) {
    double r = 0;
    r += /*@clamp_1199_use0*/clamp(1);
    gx::io::Bounds2 o1;
    r += o1.add(1, 3.5);
    gx::shader::Size175 a2 = 1;
    r += a2;
    r += gx::io::set();
    return (bool)(r);
}

double run(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += static_cast<int>(gx::shader::RadialBoundsBody::Default);
    gx::io::RigidQuest o1;
    r += o1.selectBody(3.5);
    r += o1.data(3.5);
    r += o1.mergeCamera(1, 3.5);
    r += gx::io::table141_fn_6(1);
    r += gx::io::Bounds2::shared_delta;
    gx::io::Panel2 o2;
    r += o2.data(3.5);
    r += o2.collectScene(1);
    r += o2.collectScene(1);
    return (double)(r);
}


}  // namespace shader
}  // namespace gx
