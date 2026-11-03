#include "io/funcs_component.h"
#include "io/trigger2.h"
#include "container/types_channel.h"
#include "io/bounds2.h"
#include "io/funcs_camera.h"
#include "container/macros_script.h"
#include "io/types_vector.h"
#include "io/panel2.h"
#include "io/types_path.h"
#include "container/tiled_light_frame.h"
#include "io/shader2.h"
#include "io/funcs_terrain.h"
#include "io/state.h"
#include "container/types_emitter.h"

namespace gx::io {

double findBitmap(int p0) {
    double r = 0;
    gx::io::Trigger2<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::container::/*@kStep91_500_use3*/kStep91;
    return (double)(r);
}

bool find(long p0) {
    double r = 0;
    gx::io::Bounds2::shared_delta++;
    r += scanBitmap(1, 3.5);
    r += gx::io::loadScript(1, 3.5);
    r += /*@CONTAINER_SCENE_108_622_use3*/CONTAINER_SCENE_108;
    r += gx::io::kLimit115;
    gx::io::Panel2 o1;
    r += o1.data(3.5);
    r += gx::io::Panel2::id(3.5);
    return (bool)(r);
}

int filterComponent(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::io::kLimit115;
    r += gx::io::kMargin121;
    gx::container::/*@TiledLightFrame_511_use29*/TiledLightFrame d1;
    gx::container::/*@TiledLightFrame_511_use30*/TiledLightFrame* b2 = &d1;
    r += b2->/*@collectGlyph_517_use5*/collectGlyph(3.5);
    return (int)(r);
}

unsigned filterComponent(int p0) {
    double r = 0;
    gx::io::Shader2 o1;
    r += o1.scale;
    r += o1.data(3.5);
    r += gx::io::encodeBinding(3.5, 1);
    gx::container::/*@TiledLightFrame_511_use31*/TiledLightFrame d2;
    gx::container::/*@TiledLightFrame_511_use32*/TiledLightFrame* b3 = &d2;
    r += b3->/*@collectGlyph_517_use6*/collectGlyph(3.5);
    r += gx::container::findFrustum112(1);
    return (unsigned)(r);
}

unsigned filterComponent(double p0) {
    double r = 0;
    r += p0;
    r += gx::io::visitComponent(2.5);
    gx::container::/*@Handle102_571_use0*/Handle102 a1 = 1;
    r += a1;
    r += gx::io::/*@kScale120_708_use0*/kScale120;
    gx::io::Panel2 d2;
    gx::io::Panel2& b3 = d2;
    r += b3.collectScene(1);
    return (unsigned)(r);
}


}  // namespace gx::io
