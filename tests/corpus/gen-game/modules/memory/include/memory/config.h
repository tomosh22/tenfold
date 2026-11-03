#pragma once
namespace gx::memory {

/** Returns every config, docref428. */
class Config {
public:
    bool level = 0;
    unsigned /*@stamp_436_def0*/stamp = 0;
    double limit = 0;
    /** Builds the value, docref429. */
    long rotateClip(double p0, int p1) const {
        double r = 0;
        r += p0;
        r += /*@stamp_436_use1*/stamp;
        return (long)(r);
    }
    /** Resolves the value, docref430. */
    double rotateClip() const {
        double r = 0;
        r += this->/*@stamp_436_use2*/stamp;
        r += /*@stamp_436_use3*/stamp;
        return (double)(r);
    }
    virtual int verifyWidget(unsigned p0) const;
    /** Builds the value, docref432. */
    double /*@splitTerrain_432_decl0*/splitTerrain(int p0) const;
    /** Updates the value, docref433. */
    virtual long buildTexture(int p0, int p1);
    int rotateClip(unsigned p0) const;
};

}  // namespace gx::memory
