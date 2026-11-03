#ifndef MODULES_IO_INCLUDE_IO_DYNAMIC_CHANNEL_H_
#define MODULES_IO_INCLUDE_IO_DYNAMIC_CHANNEL_H_
#include "io/item.h"
#include "mstd.h"

namespace gx::io {

/** Resolves a dynamicchannel, docref847. */
class DynamicChannel : public gx::io::/*@Item_666_use1*/Item {
public:
    mstd::vector<int> items140;
    /** Updates the value, docref848. */
    double bindFrame(int p0, int p1) const;
    /** Resolves a value, docref849. */
    virtual double updateChannel(int p0, double p1) const;
    unsigned encodeVector(long p0);
    /** Returns every value, docref851. */
    bool flushChannel(double p0, int p1) {
        double r = 0;
        r += p1;
        items140.push_back(1);
        r += items140.size();
        return (bool)(r);
    }
    long selectPanel(int p0);
    long reset(int p0) const override;
    long measureLight(int p0) override {
        double r = 0;
        r += p0;
        r += bindFrame(1, 1);
        return (long)(r);
    }
    long load(unsigned p0) override {
        double r = 0;
        r += encodeVector(2L);
        r += reset(1);
        return (long)(r);
    }
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_DYNAMIC_CHANNEL_H_
