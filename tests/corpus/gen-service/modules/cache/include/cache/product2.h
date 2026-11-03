#ifndef MODULES_CACHE_INCLUDE_CACHE_PRODUCT2_H_
#define MODULES_CACHE_INCLUDE_CACHE_PRODUCT2_H_
#include "util/static_ledger.h"

namespace svc::cache {

/** Updates a product2, docref1228. */
class Product2 : public svc::util::StaticLedger {
public:
    struct Node { int val = 0; };
    long level = 0;
    /** Updates the value, docref1229. */
    double /*@apply_1229_decl0*/apply(unsigned p0) const;
    int apply(int p0) const {
        double r = 0;
        r += p0;
        r += /*@apply_1229_use0*/apply(7u);
        r += apply(1);
        return (int)(r);
    }
    int name(int p0, int p1) {
        double r = 0;
        r += p0;
        r += p1;
        Node n1;
        r += n1.val;
        return (int)(r);
    }
};

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_PRODUCT2_H_
