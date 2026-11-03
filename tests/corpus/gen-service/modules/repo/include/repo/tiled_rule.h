#ifndef MODULES_REPO_INCLUDE_REPO_TILED_RULE_H_
#define MODULES_REPO_INCLUDE_REPO_TILED_RULE_H_
#include "repo/fast_product.h"
#include "mstd.h"

namespace svc::repo {

class TiledRule : public svc::repo::FastProduct {
public:
    struct /*@Node_952_def0*/Node { int val = 0; };
    bool level = 0;
    mstd::vector<int> items195;
    double push() const {
        double r = 0;
        r += items195.size();
        r += items195.size();
        return (double)(r);
    }
    /** Returns a value, docref949. */
    virtual int reset(double p0);
    unsigned /*@push_950_def0*/push(long p0) const {
        double r = 0;
        r += p0;
        r += items195.size();
        r += push();
        return (unsigned)(r);
    }
};

}  // namespace svc::repo

#endif  // MODULES_REPO_INCLUDE_REPO_TILED_RULE_H_
