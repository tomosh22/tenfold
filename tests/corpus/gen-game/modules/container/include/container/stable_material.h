#pragma once
namespace gx::container {

class StableMaterial {
public:
    virtual ~StableMaterial() {}
    virtual int count(long p0) const = 0;
    /** Computes a value, docref606. */
    virtual bool data(double p0) = 0;
    virtual double collectScene(int p0) = 0;
};

}  // namespace gx::container
