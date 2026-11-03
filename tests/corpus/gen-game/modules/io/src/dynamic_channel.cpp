#include "io/dynamic_channel.h"
#include "io/spawner.h"
#include "io/shader2.h"
#include "container/stable_material.h"
#include "container/funcs_terrain.h"
#include "io/rigid_quest.h"
#include "io/funcs_camera.h"
#include "io/panel2.h"
#include "container/funcs_agent.h"
#include "container/macros_script.h"
#include "io/bounds2.h"
#include "io/funcs_component.h"

namespace gx::io {


double DynamicChannel::bindFrame(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::io::findEmitter<int>(2);
    gx::io::Shader2 d1;
    gx::container::StableMaterial* b2 = &d1;
    r += b2->count(2L);
    r += gx::container::markCamera(1, 3.5);
    gx::io::RigidQuest o3;
    r += o3.mergeCamera(1, 3.5);
    r += o3.score;
    return (double)(r);
}

double DynamicChannel::updateChannel(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::io::pop(1, 1);
    r += gx::container::selectWidget();
    gx::io::Panel2 o1;
    r += gx::io::Panel2::/*@set_686_use1*/set();
    r += gx::io::pop(1, 1);
    return (double)(r);
}

unsigned DynamicChannel::encodeVector(long p0) {
    double r = 0;
    r += p0;
    r += gx::container::selectArena(3.5, 1);
    gx::io::Spawner<int> t1;
    r += t1.get();
    t1.set(3);
    r += CONTAINER_SCALE_109(2);
    r += gx::io::loadScript(1, 3.5);
    return (unsigned)(r);
}

long DynamicChannel::selectPanel(int p0) {
    double r = 0;
    gx::io::Bounds2 o1;
    r += o1.updateAnimator(1);
    r += o1.reset(1);
    items140.push_back(1);
    r += items140.size();
    return (long)(r);
}

long DynamicChannel::reset(int p0) const {
    double r = 0;
    r += p0;
    r += gx::io::shiftFrame(7u);
    r += gx::io::filterComponent(1);
    return (long)(r);
}


}  // namespace gx::io
