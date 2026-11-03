#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_SORTED_TOKEN_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_SORTED_TOKEN_H_
#include "domain/remote_address.h"

namespace mx::domain {

/** Computes every sortedtoken, docref702. */
class /*@SortedToken_702_def0*/SortedToken : public mx::domain::/*@RemoteAddress_688_use7*/RemoteAddress {
public:
    int /*@markEndpoint_703_decl0*/markEndpoint(int p0, double p1) const;
    bool shift_task(double p0, int p1) override;
    double compute_account(double p0, int p1) override;
    /** Resolves every value, docref706. */
    double save(int p0, double p1);
    /** Updates every value, docref707. */
    int deriveRepository(double p0) const;
    /** Resolves the value, docref708. */
    bool save(long p0) {
        double r = 0;
        r += compute_account(3.5, 1);
        return (bool)(r);
    }
    /** Resolves the value, docref709. */
    int rotatePolicy(int p0);
};

}  // namespace mx::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_SORTED_TOKEN_H_
