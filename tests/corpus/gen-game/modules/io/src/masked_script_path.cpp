#include "io/masked_script_path.h"
#include "io/types_prefab.h"
#include "io/funcs_shader.h"
#include "io/spawner.h"
#include "container/macros_script.h"
#include "container/types_channel.h"

namespace gx::io {

int MaskedScriptPath::shared_index = 0;
static int /*@fold_869_def0*/fold(int p0) { return p0 + 8; }

int MaskedScriptPath::resolveTexture(int p0, double p1) {
    double r = 0;
    r += p1;
    r += resolveTexture();
    r += gx::io::/*@kMargin125_725_use1*/kMargin125;
    return (int)(r);
}

bool MaskedScriptPath::saveTexture(int p0, double p1) {
    double r = 0;
    r += p0;
    r += gx::io::process();
    r += gx::io::findEmitter<int>(2);
    r += flags;
    r += /*@CONTAINER_SCENE_108_622_use5*/CONTAINER_SCENE_108;
    r += gx::container::/*@kStep91_500_use4*/kStep91;
    r += /*@fold_869_use0*/fold(1);
    return (bool)(r);
}


}  // namespace gx::io
