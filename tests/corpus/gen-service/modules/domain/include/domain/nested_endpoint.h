#pragma once
#include "util/static_ledger.h"

namespace svc::domain {

/** Builds a nestedendpoint, docref621. */
class NestedEndpoint : public svc::util::StaticLedger {
public:
    struct Entry { int val = 0; };
    unsigned /*@mass_628_def0*/mass = 0;
    bool build_endpoint();
    static double verify_tenant(int p0);
    bool /*@scan_token_624_decl0*/scan_token(double p0, int p1) const;
    /** Finds every value, docref625. */
    double collect_tariff(double p0, int p1);
    bool combineCustomer(int p0, int p1) const override;
    unsigned probe_mapper(int p0, double p1);
};

}  // namespace svc::domain
