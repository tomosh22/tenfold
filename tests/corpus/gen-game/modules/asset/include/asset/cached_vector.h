#pragma once
#include "asset/helper.h"
#include "mstd.h"

namespace gx::asset {

class CachedVector : public gx::asset::Helper {
public:
    double weight = 0;
    unsigned scale = 0;
    int width = 0;
    mstd::vector<int> items150;
    static int shared_count;
    long add(double p0, int p1) const {
        double r = 0;
        r += p1;
        r += add(3.5, 1);
        return (long)(r);
    }
    long mergeCamera(unsigned p0) const;
    unsigned resetInventory(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += items150.size();
        r += mergeCamera(7u);
        return (unsigned)(r);
    }
    unsigned loadTween() const override;
};

}  // namespace gx::asset
