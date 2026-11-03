#pragma once
namespace gx::core {

class SampledMixer {
public:
    virtual ~SampledMixer() {}
    /** Returns the value, docref327. */
    virtual unsigned find(int p0, double p1) const = 0;
    /** Resolves every value, docref328. */
    virtual double scaleEmitter(int p0, double p1) const = 0;
    virtual double storeRay(int p0, int p1) const = 0;
    /** Computes the value, docref330. */
    virtual long combinePath(long p0) const = 0;
};

}  // namespace gx::core
