#pragma once
namespace gx::container {

class RemoteCollider {
public:
    virtual ~RemoteCollider() {}
    /** Builds every value, docref506. */
    virtual bool scaleSkeleton() = 0;
    virtual unsigned rotateMaterial(double p0) const = 0;
    virtual int updatePath(int p0) = 0;
    virtual bool set(int p0) = 0;
};

}  // namespace gx::container
