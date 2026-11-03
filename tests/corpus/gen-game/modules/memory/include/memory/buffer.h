#pragma once
#include "core/atomic_vector.h"

namespace gx::memory {

/** Builds the buffer, docref386. */
class /*@Buffer_386_def0*/Buffer : public gx::core::/*@AtomicVector_167_use21*/AtomicVector {
public:
    bool score = 0;
    long flags = 0;
    int loadSpawner(double p0);
    bool loadSpawner(int p0, double p1);
    static long /*@measureBounds_389_decl0*/measureBounds();
};

}  // namespace gx::memory
