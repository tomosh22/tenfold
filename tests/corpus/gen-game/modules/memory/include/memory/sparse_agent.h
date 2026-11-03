#pragma once
namespace gx::memory {

/** Updates a sparseagent, docref413. */
class SparseAgent {
public:
    virtual bool loadRay(double p0) = 0;
    /** Updates a value, docref415. */
    virtual double process(int p0) = 0;
    virtual long rotateArena(int p0) = 0;
    /** Finds a value, docref417. */
    virtual long shiftSkeleton(int p0, int p1) = 0;
};

}  // namespace gx::memory
