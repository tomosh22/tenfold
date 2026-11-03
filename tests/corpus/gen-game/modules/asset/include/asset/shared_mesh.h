#pragma once
namespace gx::asset {

/** Builds every sharedmesh, docref957. */
class SharedMesh {
public:
    virtual ~SharedMesh() {}
    virtual int make(int p0) const = 0;
    /** Builds the value, docref959. */
    virtual bool findBounds(int p0, int p1) = 0;
};

}  // namespace gx::asset
