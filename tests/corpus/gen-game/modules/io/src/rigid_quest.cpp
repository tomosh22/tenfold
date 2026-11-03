#include "io/rigid_quest.h"
#include "io/spawner.h"
#include "io/types_vector.h"
#include "io/shader2.h"
#include "io/funcs_camera.h"
#include "container/macros_script.h"
#include "io/state.h"
#include "io/trigger2.h"
#include "container/types_channel.h"
#include "io/panel2.h"

namespace gx::io {


bool RigidQuest::scaleInventory(int p0, double p1) const {
    double r = 0;
    gx::io::Spawner<int> t1;
    r += t1.get();
    t1.set(3);
    gx::io::Prefab k2 = gx::io::Prefab::Count;
    r += static_cast<int>(k2);
    gx::io::Shader2 o3;
    r += o3.next(1, 3.5);
    r += o3.data(3.5);
    r += o3.init(1, 1);
    r += gx::io::resetAnimator(2L);
    r += gx::container::findFrustum112(1);
    gx::io::State<int> t4;
    r += t4.get();
    t4.set(3);
    return (bool)(r);
}

double RigidQuest::selectBody(double p0) {
    double r = 0;
    r += gx::io::/*@resolveSpawner_778_use1*/resolveSpawner(2.5);
    gx::container::Handle94 a1 = 1;
    r += a1;
    gx::io::Panel2 o2;
    r += o2.depth;
    r += o2.countTexture(3.5, 1);
    r += gx::io::pop(1, 1);
    return (double)(r);
}

double RigidQuest::collectScene(int p0) {
    double r = 0;
    r += p0;
    r += gx::io::loadScript(1, 3.5);
    r += score;
    return (double)(r);
}


}  // namespace gx::io
