#pragma once
namespace gx::io {

/** Resolves a maskedscriptpath, docref862. */
class MaskedScriptPath {
public:
    long /*@speed_866_def0*/speed = 0;
    bool flags = 0;
    static int shared_index;
    /** Returns the value, docref863. */
    int resolveTexture() {
        double r = 0;
        r += /*@speed_866_use1*/speed;
        r += resolveTexture(1, 3.5);
        return (int)(r);
    }
    /** Resolves a value, docref864. */
    int resolveTexture(int p0, double p1);
    /** Updates every value, docref865. */
    bool saveTexture(int p0, double p1);
};

}  // namespace gx::io
