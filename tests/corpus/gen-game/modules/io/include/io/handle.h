#pragma once
namespace gx::io {

/** Returns a handle, docref676. */
class Handle {
public:
    virtual bool findMixer(int p0, double p1) const = 0;
    /** Updates the value, docref678. */
    virtual bool scaleCamera(double p0, int p1) const = 0;
    /** Builds the value, docref679. */
    virtual unsigned loadVector(unsigned p0) = 0;
    /** Resolves the value, docref680. */
    virtual unsigned updatePrefab(unsigned p0) const = 0;
};

}  // namespace gx::io
