#ifndef MODULES_REPO_INCLUDE_REPO_SPARSE_REPOSITORY_H_
#define MODULES_REPO_INCLUDE_REPO_SPARSE_REPOSITORY_H_
#include "repo/fast_product.h"

namespace svc::repo {

class FrozenRepositoryCustomer;

/** Computes the sparserepository, docref956. */
class SparseRepository : public svc::repo::FastProduct {
public:
    struct State { int val = 0; };
    long rank = 0;
    svc::repo::FrozenRepositoryCustomer* link196 = nullptr;
    /** Resolves the value, docref957. */
    int fetchTask(double p0);
    bool loadQuota(int p0, int p1);
    double resetQuota(int p0, int p1) override {
        double r = 0;
        r += p1;
        State n1;
        r += n1.val;
        return (double)(r);
    }
    /** Returns a value, docref960. */
    long loadHandler(int p0, int p1) {
        double r = 0;
        r += p1;
        r += resetQuota(1, 1);
        r += rank;
        return (long)(r);
    }
    /** Finds the value, docref961. */
    double findRoute(double p0, int p1);
};

}  // namespace svc::repo

#endif  // MODULES_REPO_INCLUDE_REPO_SPARSE_REPOSITORY_H_
