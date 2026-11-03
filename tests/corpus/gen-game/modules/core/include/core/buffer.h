#pragma once
namespace gx::core {

class /*@Buffer_208_def0*/Buffer {
public:
    long index = 0;
    long /*@phase_216_def0*/phase = 0;
    double rotateQuest(int p0, double p1) const;
    /** Returns a value, docref210. */
    static long flush();
    /** Updates the value, docref211. */
    virtual double run(double p0, int p1);
    unsigned filterScript(double p0, int p1) const;
    int applyPrefab(long p0) const;
    unsigned collectMatrix(double p0) {
        double r = 0;
        r += p0;
        r += /*@phase_216_use0*/phase;
        r += index;
        return (unsigned)(r);
    }
};

}  // namespace gx::core
