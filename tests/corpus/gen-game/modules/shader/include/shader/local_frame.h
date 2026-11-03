#pragma once
namespace gx {
namespace shader {

/** Finds a localframe, docref1261. */
class LocalFrame {
public:
    virtual ~LocalFrame() {}
    /** Resolves a value, docref1262. */
    virtual double decodeCollider(double p0) const = 0;
    virtual long /*@scanShader_1263_decl0*/scanShader(int p0, int p1) = 0;
};

}  // namespace shader
}  // namespace gx
