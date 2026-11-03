#include "repo/tiled_rule.h"
#include "tiled_rule_priv.h"
#include "logging/macros_account.h"

namespace svc::repo {

static int sum(int p0) { return p0 + 5; }

int TiledRule::reset(double p0) {
    double r = 0;
    r += push();
    r += LOGGING_SCALE_84(2);
    /*@Node_952_use0*/Node n1;
    r += n1.val;
    return (int)(r);
}


}  // namespace svc::repo
