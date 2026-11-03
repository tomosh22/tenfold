#include "io/provider.h"
#include "provider_priv.h"
#include "io/funcs_camera.h"
#include "io/rigid_quest.h"
#include "io/panel2.h"
#include "container/types_vector.h"
#include "io/shader2.h"
#include "container/camera.h"
#include "io/funcs_shader.h"
#include "io/bounds2.h"

namespace gx::io {

static int sum(int p0) { return p0 + 6; }

unsigned Provider::selectTween(double p0, int p1) const {
    double r = 0;
    r += flush(3.5);
    r += loadTerrain(1);
    r += gx::io::pop(1, 1);
    gx::io::RigidQuest o1;
    r += o1.score;
    gx::io::Panel2 o2;
    r += o2.collectScene(1);
    r += o2.data(3.5);
    return (unsigned)(r);
}

bool Provider::verifyMatrix(unsigned p0) {
    double r = 0;
    r += p0;
    r += gx::container::kLimit96;
    gx::io::Shader2 o1;
    r += o1.load(3.5);
    r += o1.count(2L);
    r += o1.next(1, 3.5);
    r += gx::container::/*@fetchAgent_563_use3*/fetchAgent(2.5);
    r += gx::io::process();
    gx::io::Bounds2 o2;
    r += o2.reset(1);
    r += o2.add(1, 3.5);
    return (bool)(r);
}


}  // namespace gx::io
