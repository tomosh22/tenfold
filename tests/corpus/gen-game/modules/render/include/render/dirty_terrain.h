#pragma once
namespace gx {
namespace render {

/** Computes a dirtyterrain, docref1079. */
class DirtyTerrain {
public:
    virtual ~DirtyTerrain() {}
    /** Returns a value, docref1080. */
    virtual unsigned probeQuat(int p0) const = 0;
    virtual long /*@apply_1081_decl0*/apply() const = 0;
    /** Finds every value, docref1082. */
    virtual bool saveMixer(long p0) = 0;
};

}  // namespace render
}  // namespace gx
