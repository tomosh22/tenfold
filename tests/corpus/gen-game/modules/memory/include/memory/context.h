#pragma once
namespace gx::memory {

/** Finds every context, docref371. */
class Context {
public:
    virtual ~Context() {}
    virtual bool selectSprite() = 0;
    /** Updates the value, docref373. */
    virtual unsigned splitBounds(long p0) = 0;
    virtual bool sortJoint(unsigned p0) = 0;
    virtual double next(int p0, int p1) = 0;
};

}  // namespace gx::memory
