#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TARIFF_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TARIFF_H_
#include "mstd.h"

namespace svc {
namespace storage {

class Tariff {
public:
    long depth = 0;
    mstd::vector<int> items237;
    static unsigned id(double p0, int p1) {
        double r = 0;
        r += p0;
        r += id(3.5, 1);
        r += id(3.5, 1);
        return (unsigned)(r);
    }
    double rotateOrder(unsigned p0);
    /** Resolves the value, docref1153. */
    int deriveIndex(int p0) const;
    /** Builds every value, docref1154. */
    bool add(unsigned p0) const;
    unsigned shiftOrder(double p0) const;
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TARIFF_H_
