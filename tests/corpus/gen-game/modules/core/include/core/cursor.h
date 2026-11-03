#pragma once
#include "core/cyclic_arena.h"

namespace gx::core {

class CyclicArena;

class Cursor : public gx::core::CyclicArena {
public:
    unsigned weight = 0;
    gx::core::CyclicArena* link55 = nullptr;
    double mergeMesh(double p0);
    /** Builds a value, docref264. */
    static long push(unsigned p0) {
        double r = 0;
        r += p0;
        r += push(7u);
        r += push(7u);
        return (long)(r);
    }
};

}  // namespace gx::core
