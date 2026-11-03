#pragma once
namespace gx::memory {

/** Builds the entry, docref418. */
class Entry {
public:
    virtual ~Entry() {}
    bool width = 0;
    unsigned phase = 0;
    int delta = 0;
    /** Updates a value, docref419. */
    long flushSpawner(int p0) const;
    /** Updates a value, docref420. */
    int combineCamera(long p0) const;
    /** Returns every value, docref421. */
    virtual long init(int p0) const {
        double r = 0;
        r += phase;
        r += delta;
        return (long)(r);
    }
    static unsigned buildBitmap(int p0) {
        double r = 0;
        r += p0;
        r += buildBitmap(1);
        return (unsigned)(r);
    }
};

}  // namespace gx::memory
