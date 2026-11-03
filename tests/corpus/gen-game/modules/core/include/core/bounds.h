#ifndef MODULES_CORE_INCLUDE_CORE_BOUNDS_H_
#define MODULES_CORE_INCLUDE_CORE_BOUNDS_H_
#include "core/buffer.h"

namespace gx::core {

class /*@Bounds_286_def0*/Bounds : public gx::core::/*@Buffer_208_use0*/Buffer {
public:
    long height = 0;
    unsigned offset = 0;
    /** Builds a value, docref287. */
    static bool bindScene();
    unsigned bindScene(int p0, double p1) {
        double r = 0;
        r += p1;
        r += sweepAnimator(7u);
        r += bindScene(1, 3.5);
        return (unsigned)(r);
    }
    double bindScene(int p0, int p1) {
        double r = 0;
        r += p0;
        r += empty();
        r += empty();
        return (double)(r);
    }
    /** Resolves the value, docref290. */
    static unsigned empty();
    long sweepAnimator(unsigned p0);
};

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_BOUNDS_H_
