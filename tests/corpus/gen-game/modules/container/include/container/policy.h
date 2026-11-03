#pragma once
namespace gx::container {

/** Finds every policy, docref581. */
class Policy {
public:
    /** Builds the value, docref582. */
    virtual int buildQuat(unsigned p0) const = 0;
    virtual long add(unsigned p0) = 0;
    virtual bool /*@reset_584_decl0*/reset(int p0) = 0;
    /** Builds every value, docref585. */
    virtual long saveFrame(int p0) const = 0;
};

}  // namespace gx::container
